from __future__ import annotations

import io
import json
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools import cluster_ctl


class FakeClock:
    def __init__(self) -> None:
        self.value = 0.0

    def now(self) -> float:
        return self.value

    def sleep(self, seconds: float) -> None:
        self.value += seconds


class FakeProcess:
    def __init__(self, pid: int) -> None:
        self.pid = pid
        self._running = True

    def poll(self) -> int | None:
        if self._running:
            return None
        return 0


class ClusterCtlTests(unittest.TestCase):
    def test_load_cluster_plan_orders_nodes_stably(self) -> None:
        config_path = self.write_config(
            {
                "gm": {
                    "controlNetwork": {
                        "listenEndpoint": {
                            "host": "127.0.0.1",
                            "port": 5100,
                        }
                    }
                },
                "game": {
                    "Game10": {},
                    "Game2": {},
                    "Game1": {},
                },
                "gate": {
                    "Gate2": {},
                    "Gate10": {},
                    "Gate1": {},
                },
            }
        )

        plan = cluster_ctl.load_cluster_plan(config_path, repo_root=ROOT)

        self.assertEqual(
            plan.start_order,
            ("GM", "Game1", "Game2", "Game10", "Gate1", "Gate2", "Gate10"),
        )
        self.assertEqual(plan.expected_registered_process_count, 6)
        self.assertEqual(plan.control_url_base, "http://127.0.0.1:5100")

    def test_start_cluster_starts_nodes_in_expected_order(self) -> None:
        config_path = self.write_config(
            {
                "gm": {
                    "controlNetwork": {
                        "listenEndpoint": {
                            "host": "127.0.0.1",
                            "port": 5100,
                        }
                    }
                },
                "game": {
                    "Game1": {},
                    "Game0": {},
                },
                "gate": {
                    "Gate1": {},
                    "Gate0": {},
                },
            }
        )
        plan = cluster_ctl.load_cluster_plan(config_path, repo_root=ROOT)

        spawned: list[list[str]] = []
        http_calls: list[str] = []

        def fake_popen(command: list[str], **_: object) -> FakeProcess:
            spawned.append(command)
            return FakeProcess(100 + len(spawned))

        def fake_http_get_json(url: str, _: float) -> dict[str, object]:
            http_calls.append(url)
            if url.endswith("/healthz"):
                return {"status": "ok", "nodeId": "GM"}
            if url.endswith("/status"):
                return {"registeredProcessCount": 4}
            raise AssertionError(f"Unexpected URL: {url}")

        clock = FakeClock()

        cluster_ctl.start_cluster(
            plan,
            node_binary=ROOT / "build" / "src" / "native" / "node" / "Debug" / "xserver-node.exe",
            startup_timeout_seconds=1.0,
            poll_interval_seconds=0.1,
            popen_factory=fake_popen,
            http_get_json=fake_http_get_json,
            list_processes=lambda: [],
            kill_process_tree=lambda pid: None,
            clock=clock.now,
            sleep=clock.sleep,
        )

        self.assertEqual(
            [command[2] for command in spawned],
            ["GM", "Game0", "Game1", "Gate0", "Gate1"],
        )
        self.assertEqual(
            http_calls,
            [
                "http://127.0.0.1:5100/healthz",
                "http://127.0.0.1:5100/status",
            ],
        )

    def test_start_cluster_reports_startup_flow_and_stub_distribution(self) -> None:
        config_path = self.write_config(
            {
                "gm": {
                    "controlNetwork": {
                        "listenEndpoint": {
                            "host": "127.0.0.1",
                            "port": 5100,
                        }
                    }
                },
                "game": {
                    "Game1": {},
                    "Game0": {},
                },
                "gate": {
                    "Gate0": {},
                },
            }
        )
        plan = cluster_ctl.load_cluster_plan(config_path, repo_root=ROOT)

        spawned: list[list[str]] = []
        status_index = 0
        status_payloads = [
            {
                "nodeId": "GM",
                "innerNetworkEndpoint": "tcp://127.0.0.1:5000",
                "controlNetworkEndpoint": "127.0.0.1:5100",
                "registeredProcessCount": 3,
                "running": True,
                "startupFlow": {
                    "expectedGameCount": 2,
                    "expectedGateCount": 1,
                    "registeredGameCount": 2,
                    "registeredGateCount": 1,
                    "allNodesOnline": False,
                    "allExpectedGamesMeshReady": False,
                    "catalogLoaded": False,
                    "catalogLoadFailed": False,
                    "ownershipActive": False,
                    "assignmentEpoch": 0,
                    "totalStubCount": 0,
                    "assignedStubCount": 0,
                    "readyStubCount": 0,
                    "readyEpoch": 0,
                    "clusterReady": False,
                },
                "nodes": [
                    {"nodeId": "Game0", "registered": True, "online": True, "lastProtocolError": ""},
                    {
                        "nodeId": "Game1",
                        "registered": True,
                        "online": True,
                        "lastProtocolError": "waiting for Gate0 mesh closure",
                    },
                    {"nodeId": "Gate0", "registered": True, "online": True, "lastProtocolError": ""},
                ],
                "gameMeshReady": [
                    {"nodeId": "Game0", "meshReady": False},
                    {"nodeId": "Game1", "meshReady": False},
                ],
                "stubDistribution": [
                    {"nodeId": "Game0", "ownedStubCount": 0, "readyStubCount": 0, "stubs": []},
                    {"nodeId": "Game1", "ownedStubCount": 0, "readyStubCount": 0, "stubs": []},
                ],
            },
            {
                "nodeId": "GM",
                "innerNetworkEndpoint": "tcp://127.0.0.1:5000",
                "controlNetworkEndpoint": "127.0.0.1:5100",
                "registeredProcessCount": 3,
                "running": True,
                "startupFlow": {
                    "expectedGameCount": 2,
                    "expectedGateCount": 1,
                    "registeredGameCount": 2,
                    "registeredGateCount": 1,
                    "allNodesOnline": True,
                    "allExpectedGamesMeshReady": False,
                    "catalogLoaded": True,
                    "catalogLoadFailed": False,
                    "ownershipActive": False,
                    "assignmentEpoch": 0,
                    "totalStubCount": 3,
                    "assignedStubCount": 0,
                    "readyStubCount": 0,
                    "readyEpoch": 0,
                    "clusterReady": False,
                },
                "nodes": [
                    {"nodeId": "Game0", "registered": True, "online": True, "lastProtocolError": ""},
                    {
                        "nodeId": "Game1",
                        "registered": True,
                        "online": True,
                        "lastProtocolError": "waiting for Gate0 mesh closure",
                    },
                    {"nodeId": "Gate0", "registered": True, "online": True, "lastProtocolError": ""},
                ],
                "gameMeshReady": [
                    {"nodeId": "Game0", "meshReady": True},
                    {"nodeId": "Game1", "meshReady": False},
                ],
                "stubDistribution": [
                    {"nodeId": "Game0", "ownedStubCount": 0, "readyStubCount": 0, "stubs": []},
                    {"nodeId": "Game1", "ownedStubCount": 0, "readyStubCount": 0, "stubs": []},
                ],
            },
            {
                "nodeId": "GM",
                "innerNetworkEndpoint": "tcp://127.0.0.1:5000",
                "controlNetworkEndpoint": "127.0.0.1:5100",
                "registeredProcessCount": 3,
                "running": True,
                "startupFlow": {
                    "expectedGameCount": 2,
                    "expectedGateCount": 1,
                    "registeredGameCount": 2,
                    "registeredGateCount": 1,
                    "allNodesOnline": True,
                    "allExpectedGamesMeshReady": True,
                    "catalogLoaded": True,
                    "catalogLoadFailed": False,
                    "ownershipActive": True,
                    "assignmentEpoch": 1,
                    "totalStubCount": 3,
                    "assignedStubCount": 3,
                    "readyStubCount": 2,
                    "readyEpoch": 0,
                    "clusterReady": False,
                },
                "nodes": [
                    {"nodeId": "Game0", "registered": True, "online": True, "lastProtocolError": ""},
                    {"nodeId": "Game1", "registered": True, "online": True, "lastProtocolError": ""},
                    {"nodeId": "Gate0", "registered": True, "online": True, "lastProtocolError": ""},
                ],
                "gameMeshReady": [
                    {"nodeId": "Game0", "meshReady": True},
                    {"nodeId": "Game1", "meshReady": True},
                ],
                "stubDistribution": [
                    {
                        "nodeId": "Game0",
                        "ownedStubCount": 2,
                        "readyStubCount": 1,
                        "stubs": [
                            {"entityType": "MatchStub", "state": "Ready"},
                            {"entityType": "LeaderboardStub", "state": "Init"},
                        ],
                    },
                    {
                        "nodeId": "Game1",
                        "ownedStubCount": 1,
                        "readyStubCount": 1,
                        "stubs": [
                            {"entityType": "ChatStub", "state": "Ready"},
                        ],
                    },
                ],
            },
            {
                "nodeId": "GM",
                "innerNetworkEndpoint": "tcp://127.0.0.1:5000",
                "controlNetworkEndpoint": "127.0.0.1:5100",
                "registeredProcessCount": 3,
                "running": True,
                "startupFlow": {
                    "expectedGameCount": 2,
                    "expectedGateCount": 1,
                    "registeredGameCount": 2,
                    "registeredGateCount": 1,
                    "allNodesOnline": True,
                    "allExpectedGamesMeshReady": True,
                    "catalogLoaded": True,
                    "catalogLoadFailed": False,
                    "ownershipActive": True,
                    "assignmentEpoch": 1,
                    "totalStubCount": 3,
                    "assignedStubCount": 3,
                    "readyStubCount": 3,
                    "readyEpoch": 1,
                    "clusterReady": True,
                },
                "nodes": [
                    {"nodeId": "Game0", "registered": True, "online": True, "lastProtocolError": ""},
                    {"nodeId": "Game1", "registered": True, "online": True, "lastProtocolError": ""},
                    {"nodeId": "Gate0", "registered": True, "online": True, "lastProtocolError": ""},
                ],
                "gameMeshReady": [
                    {"nodeId": "Game0", "meshReady": True},
                    {"nodeId": "Game1", "meshReady": True},
                ],
                "stubDistribution": [
                    {
                        "nodeId": "Game0",
                        "ownedStubCount": 2,
                        "readyStubCount": 2,
                        "stubs": [
                            {"entityType": "MatchStub", "state": "Ready"},
                            {"entityType": "LeaderboardStub", "state": "Ready"},
                        ],
                    },
                    {
                        "nodeId": "Game1",
                        "ownedStubCount": 1,
                        "readyStubCount": 1,
                        "stubs": [
                            {"entityType": "ChatStub", "state": "Ready"},
                        ],
                    },
                ],
            },
        ]

        def fake_popen(command: list[str], **_: object) -> FakeProcess:
            spawned.append(command)
            return FakeProcess(300 + len(spawned))

        def fake_http_get_json(url: str, _: float) -> dict[str, object]:
            nonlocal status_index
            if url.endswith("/healthz"):
                return {"status": "ok", "nodeId": "GM"}
            if not url.endswith("/status"):
                raise AssertionError(f"Unexpected URL: {url}")

            payload = status_payloads[min(status_index, len(status_payloads) - 1)]
            status_index += 1
            return payload

        clock = FakeClock()
        stdout = io.StringIO()
        with redirect_stdout(stdout):
            cluster_ctl.start_cluster(
                plan,
                node_binary=ROOT / "build" / "src" / "native" / "node" / "Debug" / "xserver-node.exe",
                startup_timeout_seconds=1.0,
                poll_interval_seconds=0.1,
                popen_factory=fake_popen,
                http_get_json=fake_http_get_json,
                list_processes=lambda: [],
                kill_process_tree=lambda pid: None,
                clock=clock.now,
                sleep=clock.sleep,
            )

        output = stdout.getvalue()
        self.assertEqual(
            [command[2] for command in spawned],
            ["GM", "Game0", "Game1", "Gate0"],
        )
        self.assertIn("[1/7] Confirmed GM control endpoint: GM /healthz=ok at http://127.0.0.1:5100", output)
        self.assertIn(
            "[2/7] Confirmed Game/Gate -> GM register and heartbeat: Game 2/2, Gate 1/1; registered=Game0, Game1, Gate0",
            output,
        )
        self.assertIn(
            "[3/7] Waiting GM -> Game allNodesOnline: allNodesOnline=false; online=Game0, Game1, Gate0",
            output,
        )
        self.assertIn(
            "[3/7] Confirmed GM -> Game allNodesOnline: allNodesOnline=true; online=Game0, Game1, Gate0",
            output,
        )
        self.assertIn(
            "[4/7] Waiting Game -> GM mesh ready: ready=1/2; meshReady=Game0; pending=Game1",
            output,
        )
        self.assertIn(
            "    Game1: gmRegistered=true, gmOnline=true, meshReady=false, reportedAtUnixMs=0, lastProtocolError=waiting for Gate0 mesh closure",
            output,
        )
        self.assertIn(
            "[4/7] Confirmed Game -> GM mesh ready: ready=2/2; meshReady=Game0, Game1",
            output,
        )
        self.assertIn(
            "[5/7] Confirmed GM -> Game ServerStubOwnershipSync: assignmentEpoch=1, assigned=3/3",
            output,
        )
        self.assertIn("[6/7] Waiting Game -> GM GameServiceReadyReport: ready stubs 2/3", output)
        self.assertIn("[6/7] Confirmed Game -> GM GameServiceReadyReport: ready stubs 3/3", output)
        self.assertIn(
            "[6/7] Confirmed Game -> GM GameServiceReadyReport: ready stubs 3/3\n"
            "    Game0: owned=2, ready=2 -> MatchStub[Ready], LeaderboardStub[Ready]\n"
            "    Game1: owned=1, ready=1 -> ChatStub[Ready]",
            output,
        )
        self.assertNotIn("pending stubs ->", output)
        self.assertIn("[7/7] Confirmed GM -> Gate ClusterReadyNotify: clusterReady=true, readyEpoch=1", output)
        self.assertNotIn("Startup flow status:", output)
        self.assertIn("Cluster startup completed. clusterReady=true, readyEpoch=1, readyStubs=3/3.", output)

    def test_start_cluster_cleans_up_started_processes_after_timeout(self) -> None:
        config_path = self.write_config(
            {
                "gm": {
                    "controlNetwork": {
                        "listenEndpoint": {
                            "host": "127.0.0.1",
                            "port": 5100,
                        }
                    }
                },
                "game": {
                    "Game0": {},
                },
                "gate": {
                    "Gate0": {},
                },
            }
        )
        plan = cluster_ctl.load_cluster_plan(config_path, repo_root=ROOT)

        killed_pids: list[int] = []
        next_pid = 200

        def fake_popen(command: list[str], **_: object) -> FakeProcess:
            nonlocal next_pid
            next_pid += 1
            return FakeProcess(next_pid)

        def fake_http_get_json(url: str, _: float) -> dict[str, object]:
            if url.endswith("/healthz"):
                return {"status": "ok"}
            if url.endswith("/status"):
                return {"registeredProcessCount": 0}
            raise AssertionError(f"Unexpected URL: {url}")

        clock = FakeClock()

        with self.assertRaises(cluster_ctl.ClusterCtlError):
            cluster_ctl.start_cluster(
                plan,
                node_binary=ROOT / "build" / "src" / "native" / "node" / "Debug" / "xserver-node.exe",
                startup_timeout_seconds=0.3,
                poll_interval_seconds=0.1,
                popen_factory=fake_popen,
                http_get_json=fake_http_get_json,
                list_processes=lambda: [],
                kill_process_tree=killed_pids.append,
                clock=clock.now,
                sleep=clock.sleep,
            )

        self.assertEqual(killed_pids, [201, 202, 203])

    def test_kill_cluster_matches_config_path_and_node_id_precisely(self) -> None:
        config_path = self.write_config(
            {
                "gm": {
                    "controlNetwork": {
                        "listenEndpoint": {
                            "host": "127.0.0.1",
                            "port": 5100,
                        }
                    }
                },
                "game": {
                    "Game0": {},
                },
                "gate": {
                    "Gate0": {},
                },
            }
        )
        other_config_path = self.write_config(
            {
                "gm": {
                    "controlNetwork": {
                        "listenEndpoint": {
                            "host": "127.0.0.1",
                            "port": 5200,
                        }
                    }
                },
                "game": {
                    "Game0": {},
                },
                "gate": {
                    "Gate0": {},
                },
            }
        )
        plan = cluster_ctl.load_cluster_plan(config_path, repo_root=ROOT)

        def make_command_line(config: Path, node_id: str) -> str:
            return subprocess.list2cmdline(
                [
                    str(ROOT / "build" / "src" / "native" / "node" / "Debug" / "xserver-node.exe"),
                    str(config),
                    node_id,
                ]
            )

        processes = [
            cluster_ctl.ProcessSnapshot(
                pid=10,
                name="xserver-node.exe",
                command_line=make_command_line(config_path, "GM"),
            ),
            cluster_ctl.ProcessSnapshot(
                pid=11,
                name="xserver-node.exe",
                command_line=make_command_line(config_path, "Game0"),
            ),
            cluster_ctl.ProcessSnapshot(
                pid=12,
                name="xserver-node.exe",
                command_line=make_command_line(config_path, "Gate0"),
            ),
            cluster_ctl.ProcessSnapshot(
                pid=13,
                name="xserver-node.exe",
                command_line=make_command_line(other_config_path, "Gate0"),
            ),
            cluster_ctl.ProcessSnapshot(
                pid=14,
                name="xserver-node.exe",
                command_line=make_command_line(config_path, "Gate99"),
            ),
            cluster_ctl.ProcessSnapshot(
                pid=15,
                name="other.exe",
                command_line=make_command_line(config_path, "Gate0"),
            ),
        ]

        killed_pids: list[int] = []
        returned_pids = cluster_ctl.kill_cluster(
            plan,
            list_processes=lambda: processes,
            kill_process_tree=killed_pids.append,
        )

        self.assertEqual(returned_pids, [10, 11, 12])
        self.assertEqual(killed_pids, [10, 11, 12])

    def write_config(self, body: dict[str, object]) -> Path:
        data = {
            "env": {
                "id": "local-dev",
                "environment": "dev",
            },
            "logging": {
                "rootDir": "logs",
                "minLevel": "Info",
                "flushIntervalMs": 1000,
                "rotateDaily": True,
                "maxFileSizeMB": 64,
                "maxRetainedFiles": 10,
            },
            "kcp": {
                "mtu": 1200,
                "sndwnd": 128,
                "rcvwnd": 128,
                "nodelay": True,
                "intervalMs": 10,
                "fastResend": 2,
                "noCongestionWindow": False,
                "minRtoMs": 30,
                "deadLinkCount": 20,
                "streamMode": False,
            },
            **body,
        }

        temp_dir = Path(tempfile.mkdtemp(prefix="xs_cluster_ctl_test_"))
        path = temp_dir / "config.json"
        path.write_text(json.dumps(data), encoding="utf-8")
        return path


if __name__ == "__main__":
    unittest.main()
