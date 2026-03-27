#!/usr/bin/env python3

from __future__ import annotations

import argparse
import ctypes
import json
import os
import shlex
import subprocess
import sys
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import Callable
from typing import Iterable
from typing import Sequence


NODE_BINARY_NAME = "xserver-node.exe"
DEFAULT_POLL_INTERVAL_SECONDS = 0.2
DEFAULT_STARTUP_TIMEOUT_SECONDS = 30.0
DEFAULT_HTTP_TIMEOUT_SECONDS = 1.0
WILDCARD_HOSTS = {"0.0.0.0", "::", "[::]"}


class ClusterCtlError(RuntimeError):
    pass


@dataclass(frozen=True)
class ProcessSnapshot:
    pid: int
    name: str
    command_line: str


@dataclass(frozen=True)
class ClusterPlan:
    repo_root: Path
    config_path: Path
    control_url_base: str
    node_ids: tuple[str, ...]
    start_order: tuple[str, ...]
    expected_registered_process_count: int


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Manage local XServerByAI cluster processes.")
    subparsers = parser.add_subparsers(dest="command", required=True)

    start_parser = subparsers.add_parser("start", help="Start a local cluster.")
    add_config_argument(start_parser)
    add_node_binary_argument(start_parser)
    start_parser.add_argument(
        "--startup-timeout",
        type=float,
        default=DEFAULT_STARTUP_TIMEOUT_SECONDS,
        help="Maximum number of seconds to wait for startup orchestration.",
    )
    start_parser.add_argument(
        "--poll-interval",
        type=float,
        default=DEFAULT_POLL_INTERVAL_SECONDS,
        help="Polling interval in seconds for GM health/status checks.",
    )

    kill_parser = subparsers.add_parser("kill", help="Force kill matching local cluster processes.")
    add_config_argument(kill_parser)

    return parser.parse_args(argv)


def add_config_argument(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--config",
        required=True,
        help="Path to the cluster JSON configuration file.",
    )


def add_node_binary_argument(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--node-bin",
        default=None,
        help="Optional path to xserver-node.exe. If omitted, common build outputs are searched.",
    )


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv or sys.argv[1:])
    repo_root = Path(__file__).resolve().parent.parent
    plan = load_cluster_plan(Path(args.config), repo_root=repo_root)

    try:
        if args.command == "start":
            node_binary = resolve_node_binary(repo_root, explicit_path=args.node_bin)
            start_cluster(
                plan,
                node_binary=node_binary,
                startup_timeout_seconds=args.startup_timeout,
                poll_interval_seconds=args.poll_interval,
            )
            return 0

        if args.command == "kill":
            killed_pids = kill_cluster(plan)
            if killed_pids:
                print("Killed processes:", ", ".join(str(pid) for pid in killed_pids))
            else:
                print("No matching processes were found.")
            return 0
    except ClusterCtlError as exc:
        print(f"cluster_ctl error: {exc}", file=sys.stderr)
        return 1

    raise AssertionError(f"Unsupported command: {args.command}")


def load_cluster_plan(config_path: Path, *, repo_root: Path) -> ClusterPlan:
    absolute_config_path = config_path.expanduser().resolve(strict=False)
    try:
        config = json.loads(absolute_config_path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise ClusterCtlError(f"Config file does not exist: {absolute_config_path}") from exc
    except json.JSONDecodeError as exc:
        raise ClusterCtlError(f"Config file is not valid JSON: {absolute_config_path}") from exc

    try:
        gm_control_endpoint = config["gm"]["controlNetwork"]["listenEndpoint"]
        gate_ids = sorted(config["gate"].keys(), key=node_sort_key)
        game_ids = sorted(config["game"].keys(), key=node_sort_key)
    except KeyError as exc:
        raise ClusterCtlError(f"Required config field is missing: {exc}") from exc
    except AttributeError as exc:
        raise ClusterCtlError("Config structure for gm/gate/game is invalid.") from exc

    if not gate_ids:
        raise ClusterCtlError("Config must define at least one Gate node.")
    if not game_ids:
        raise ClusterCtlError("Config must define at least one Game node.")

    control_url_base = build_control_url_base(gm_control_endpoint)
    node_ids = ("GM", *game_ids, *gate_ids)
    start_order = node_ids

    return ClusterPlan(
        repo_root=repo_root,
        config_path=absolute_config_path,
        control_url_base=control_url_base,
        node_ids=node_ids,
        start_order=start_order,
        expected_registered_process_count=len(game_ids) + len(gate_ids),
    )


def build_control_url_base(endpoint: dict[str, object]) -> str:
    host_value = str(endpoint["host"])
    port_value = int(endpoint["port"])
    if port_value <= 0:
        raise ClusterCtlError("GM controlNetwork.listenEndpoint.port must be greater than zero.")

    host = host_value.strip()
    if not host:
        raise ClusterCtlError("GM controlNetwork.listenEndpoint.host must not be empty.")

    if host.lower() in WILDCARD_HOSTS:
        host = "127.0.0.1" if ":" not in host else "::1"

    if ":" in host and not host.startswith("["):
        host = f"[{host}]"

    return f"http://{host}:{port_value}"


def node_sort_key(node_id: str) -> tuple[str, int | str]:
    prefix = node_id.rstrip("0123456789")
    suffix = node_id[len(prefix) :]
    if suffix.isdigit():
        return (prefix, int(suffix))
    return (prefix, suffix)


def resolve_node_binary(repo_root: Path, *, explicit_path: str | None) -> Path:
    if explicit_path:
        node_binary = Path(explicit_path).expanduser().resolve(strict=False)
        if node_binary.is_file():
            return node_binary
        raise ClusterCtlError(f"Node binary was not found: {node_binary}")

    candidates = (
        repo_root / "build" / "src" / "native" / "node" / "Debug" / NODE_BINARY_NAME,
        repo_root / "build" / "src" / "native" / "node" / "Release" / NODE_BINARY_NAME,
        repo_root / "out" / "build" / "x64-Debug" / "src" / "native" / "node" / NODE_BINARY_NAME,
        repo_root / "out" / "build" / "x64-Release" / "src" / "native" / "node" / NODE_BINARY_NAME,
    )

    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve(strict=False)

    searched = ", ".join(str(candidate) for candidate in candidates)
    raise ClusterCtlError(
        f"Unable to locate {NODE_BINARY_NAME}. Pass --node-bin explicitly. Searched: {searched}"
    )


def start_cluster(
    plan: ClusterPlan,
    *,
    node_binary: Path,
    startup_timeout_seconds: float,
    poll_interval_seconds: float,
    popen_factory: Callable[..., object] = subprocess.Popen,
    http_get_json: Callable[[str, float], dict[str, object]] | None = None,
    list_processes: Callable[[], list[ProcessSnapshot]] | None = None,
    kill_process_tree: Callable[[int], None] | None = None,
    clock: Callable[[], float] = time.monotonic,
    sleep: Callable[[float], None] = time.sleep,
) -> None:
    if startup_timeout_seconds <= 0:
        raise ClusterCtlError("--startup-timeout must be greater than zero.")
    if poll_interval_seconds < 0:
        raise ClusterCtlError("--poll-interval must not be negative.")

    http_get_json = http_get_json or fetch_json
    list_processes = list_processes or list_windows_processes
    kill_process_tree = kill_process_tree or taskkill_process_tree

    started_processes: list[object] = []
    deadline = clock() + startup_timeout_seconds

    try:
        print(f"Using node binary: {node_binary}")
        for node_id in plan.start_order:
            print(f"Starting {node_id} ...")
            process = spawn_node(
                node_binary=node_binary,
                config_path=plan.config_path,
                node_id=node_id,
                working_directory=plan.repo_root,
                popen_factory=popen_factory,
            )
            started_processes.append(process)

            if node_id == "GM":
                wait_for_gm_health(
                    plan.control_url_base,
                    deadline=deadline,
                    poll_interval_seconds=poll_interval_seconds,
                    http_get_json=http_get_json,
                    clock=clock,
                    sleep=sleep,
                )

        wait_for_registered_processes(
            plan.control_url_base,
            expected_count=plan.expected_registered_process_count,
            deadline=deadline,
            poll_interval_seconds=poll_interval_seconds,
            http_get_json=http_get_json,
            clock=clock,
            sleep=sleep,
        )
    except Exception:
        try:
            cleanup_started_processes(
                plan,
                started_processes,
                list_processes=list_processes,
                kill_process_tree=kill_process_tree,
            )
        except Exception as cleanup_exc:
            print(f"cluster_ctl cleanup warning: {cleanup_exc}", file=sys.stderr)
        raise

    print(
        "Cluster startup completed. "
        f"GM registeredProcessCount reached {plan.expected_registered_process_count}."
    )


def spawn_node(
    *,
    node_binary: Path,
    config_path: Path,
    node_id: str,
    working_directory: Path,
    popen_factory: Callable[..., object],
) -> object:
    command = [str(node_binary), str(config_path), node_id]
    kwargs = {
        "cwd": str(working_directory),
    }

    if os.name == "nt":
        creationflags = getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0)
        if creationflags:
            kwargs["creationflags"] = creationflags

    try:
        return popen_factory(command, **kwargs)
    except OSError as exc:
        raise ClusterCtlError(f"Failed to start {node_id}: {exc}") from exc


def wait_for_gm_health(
    control_url_base: str,
    *,
    deadline: float,
    poll_interval_seconds: float,
    http_get_json: Callable[[str, float], dict[str, object]],
    clock: Callable[[], float],
    sleep: Callable[[float], None],
) -> None:
    health_url = f"{control_url_base}/healthz"
    last_problem = "GM control endpoint is not ready yet."

    while clock() <= deadline:
        try:
            payload = http_get_json(health_url, DEFAULT_HTTP_TIMEOUT_SECONDS)
            if payload.get("status") == "ok":
                print("GM control endpoint is healthy.")
                return
            last_problem = f"Unexpected /healthz payload: {payload!r}"
        except Exception as exc:
            last_problem = str(exc)

        sleep(poll_interval_seconds)

    raise ClusterCtlError(f"Timed out waiting for GM /healthz: {last_problem}")


def wait_for_registered_processes(
    control_url_base: str,
    *,
    expected_count: int,
    deadline: float,
    poll_interval_seconds: float,
    http_get_json: Callable[[str, float], dict[str, object]],
    clock: Callable[[], float],
    sleep: Callable[[float], None],
) -> None:
    if expected_count <= 0:
        return

    status_url = f"{control_url_base}/status"
    last_problem = "GM /status has not reached the expected process count."

    while clock() <= deadline:
        try:
            payload = http_get_json(status_url, DEFAULT_HTTP_TIMEOUT_SECONDS)
            registered = int(payload.get("registeredProcessCount", -1))
            if registered >= expected_count:
                print(f"GM registeredProcessCount = {registered}.")
                return
            last_problem = (
                f"GM registeredProcessCount is {registered}, expected at least {expected_count}."
            )
        except Exception as exc:
            last_problem = str(exc)

        sleep(poll_interval_seconds)

    raise ClusterCtlError(f"Timed out waiting for GM /status: {last_problem}")


def fetch_json(url: str, timeout_seconds: float) -> dict[str, object]:
    request = urllib.request.Request(
        url,
        headers={"Accept": "application/json"},
        method="GET",
    )

    try:
        with urllib.request.urlopen(request, timeout=timeout_seconds) as response:
            body = response.read()
    except urllib.error.URLError as exc:
        raise ClusterCtlError(f"HTTP request failed for {url}: {exc}") from exc

    try:
        payload = json.loads(body.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ClusterCtlError(f"HTTP response is not valid JSON for {url}.") from exc

    if not isinstance(payload, dict):
        raise ClusterCtlError(f"HTTP response payload is not a JSON object for {url}.")

    return payload


def kill_cluster(
    plan: ClusterPlan,
    *,
    list_processes: Callable[[], list[ProcessSnapshot]] | None = None,
    kill_process_tree: Callable[[int], None] | None = None,
) -> list[int]:
    list_processes = list_processes or list_windows_processes
    kill_process_tree = kill_process_tree or taskkill_process_tree

    matches = select_matching_processes(
        list_processes(),
        config_path=plan.config_path,
        target_node_ids=plan.node_ids,
    )

    killed_pids: list[int] = []
    for process in matches:
        kill_process_tree(process.pid)
        killed_pids.append(process.pid)

    return killed_pids


def cleanup_started_processes(
    plan: ClusterPlan,
    started_processes: Sequence[object],
    *,
    list_processes: Callable[[], list[ProcessSnapshot]],
    kill_process_tree: Callable[[int], None],
) -> None:
    killed = set(
        kill_cluster(
            plan,
            list_processes=list_processes,
            kill_process_tree=kill_process_tree,
        )
    )

    for process in started_processes:
        pid = getattr(process, "pid", None)
        if not isinstance(pid, int) or pid in killed:
            continue

        poll = getattr(process, "poll", None)
        try:
            running = poll is None or poll() is None
        except Exception:
            running = True

        if running:
            kill_process_tree(pid)
            killed.add(pid)


def select_matching_processes(
    processes: Iterable[ProcessSnapshot],
    *,
    config_path: Path,
    target_node_ids: Iterable[str],
) -> list[ProcessSnapshot]:
    normalized_config_path = normalize_match_path(config_path)
    expected_node_ids = set(target_node_ids)
    matches: list[ProcessSnapshot] = []

    for process in processes:
        argv = split_command_line(process.command_line)
        if len(argv) < 3:
            continue

        executable_name = Path(argv[0]).name.lower()
        process_name = Path(process.name).name.lower()
        if process_name and process_name != NODE_BINARY_NAME.lower():
            continue
        if executable_name != NODE_BINARY_NAME.lower():
            continue

        process_config_path = normalize_match_path(argv[1])
        process_node_id = argv[2]

        if process_config_path != normalized_config_path:
            continue
        if process_node_id not in expected_node_ids:
            continue

        matches.append(process)

    matches.sort(key=lambda item: item.pid)
    return matches


def normalize_match_path(path_like: str | Path) -> str:
    text = str(path_like).strip().strip('"')
    if not text:
        return ""

    normalized = os.path.abspath(text)
    if os.name == "nt":
        normalized = str(Path(normalized).resolve(strict=False))

    return os.path.normcase(os.path.normpath(normalized))


def split_command_line(command_line: str) -> list[str]:
    if not command_line:
        return []

    if os.name == "nt":
        try:
            return split_windows_command_line(command_line)
        except OSError:
            pass

    parts = shlex.split(command_line, posix=False)
    return [part.strip('"') for part in parts]


def split_windows_command_line(command_line: str) -> list[str]:
    shell32 = ctypes.windll.shell32
    kernel32 = ctypes.windll.kernel32

    shell32.CommandLineToArgvW.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int)]
    shell32.CommandLineToArgvW.restype = ctypes.POINTER(ctypes.c_wchar_p)
    kernel32.LocalFree.argtypes = [ctypes.c_void_p]
    kernel32.LocalFree.restype = ctypes.c_void_p

    argc = ctypes.c_int(0)
    argv = shell32.CommandLineToArgvW(command_line, ctypes.byref(argc))
    if not argv:
        raise OSError("CommandLineToArgvW failed.")

    try:
        return [argv[index] for index in range(argc.value)]
    finally:
        kernel32.LocalFree(argv)


def list_windows_processes() -> list[ProcessSnapshot]:
    if os.name != "nt":
        raise ClusterCtlError("Process enumeration is currently only implemented for Windows.")

    command = [
        "powershell.exe",
        "-NoProfile",
        "-NonInteractive",
        "-Command",
        (
            "Get-CimInstance Win32_Process "
            "| Select-Object ProcessId, Name, CommandLine "
            "| ConvertTo-Json -Compress"
        ),
    ]

    try:
        result = subprocess.run(
            command,
            check=False,
            capture_output=True,
            text=True,
        )
    except OSError as exc:
        raise ClusterCtlError(f"Failed to enumerate Windows processes: {exc}") from exc

    if result.returncode != 0:
        stderr = result.stderr.strip()
        raise ClusterCtlError(f"Failed to enumerate Windows processes: {stderr}")

    stdout = result.stdout.strip()
    if not stdout:
        return []

    try:
        payload = json.loads(stdout)
    except json.JSONDecodeError as exc:
        raise ClusterCtlError("Failed to parse Windows process list JSON.") from exc

    if isinstance(payload, dict):
        payload = [payload]
    if not isinstance(payload, list):
        raise ClusterCtlError("Unexpected Windows process list payload.")

    snapshots: list[ProcessSnapshot] = []
    for entry in payload:
        if not isinstance(entry, dict):
            continue
        pid = entry.get("ProcessId")
        name = entry.get("Name")
        command_line = entry.get("CommandLine")
        if not isinstance(pid, int):
            continue
        snapshots.append(
            ProcessSnapshot(
                pid=pid,
                name=str(name or ""),
                command_line=str(command_line or ""),
            )
        )

    return snapshots


def taskkill_process_tree(pid: int) -> None:
    if os.name != "nt":
        raise ClusterCtlError("Process termination is currently only implemented for Windows.")

    try:
        result = subprocess.run(
            ["taskkill.exe", "/PID", str(pid), "/T", "/F"],
            check=False,
            capture_output=True,
            text=True,
        )
    except OSError as exc:
        raise ClusterCtlError(f"Failed to run taskkill for PID {pid}: {exc}") from exc

    if result.returncode != 0:
        stderr = result.stderr.strip()
        stdout = result.stdout.strip()
        detail = stderr or stdout or "taskkill returned a non-zero exit code."
        raise ClusterCtlError(f"Failed to kill PID {pid}: {detail}")


if __name__ == "__main__":
    raise SystemExit(main())
