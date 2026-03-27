from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
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
