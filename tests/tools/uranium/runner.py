#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information
#  regarding copyright ownership.  The ASF licenses this file
#  to you under the Apache License, Version 2.0 (the
#  "License"); you may not use this file except in compliance
#  with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.
"""Single entry point and container launcher for ATS Uranium tests."""

from __future__ import annotations

from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path
import hashlib
import os
import shlex
import shutil
import stat
import subprocess
import sys

DEFAULT_IMAGE = "ci.trafficserver.apache.org/ats/fedora:44"


class RunnerError(RuntimeError):
    """Report invalid runner arguments or an unavailable execution environment."""


@dataclass(frozen=True)
class ConfiguredBuild:
    """Paths substituted into the generated build-tree wrapper."""

    source_root: Path
    build_root: Path
    install_prefix: Path
    verifier_bin: Path
    sandbox: Path
    project_directory: Path
    curl_uds: bool = False


def main(arguments: Sequence[str] | None = None) -> int:
    """Dispatch a source-tree or configured-build invocation."""

    args = list(sys.argv[1:] if arguments is None else arguments)
    try:
        if not args:
            raise RunnerError("The ATS test runner requires an internal source or configured mode")
        entry_mode = args.pop(0)
        if entry_mode == "source":
            if not args:
                raise RunnerError("The source runner requires the repository root")
            return run_from_source(Path(args.pop(0)), args)
        if entry_mode == "configured":
            config, args = _parse_configured_build(args)
            return run_configured(config, args)
        raise RunnerError(f"Unknown ATS test runner mode: {entry_mode}")
    except RunnerError as error:
        print(f"urtest.sh: {error}", file=sys.stderr)
        return 2
    except OSError as error:
        print(f"urtest.sh: {error}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        print("urtest.sh: interrupted", file=sys.stderr)
        return 130


def run_from_source(source_root: Path, arguments: Sequence[str]) -> int:
    """Build ATS in the official environment, then invoke the configured runner."""

    source_root = source_root.resolve()
    should_use_docker, test_arguments = choose_docker_mode(arguments)
    if should_use_docker:
        return launch_docker(source_root, test_arguments)

    build_root = Path(os.environ.get("ATS_URTEST_CONTAINER_BUILD", source_root / "build-urtest-container")).resolve()
    install_prefix = build_root / "install"
    sandbox = _short_sandbox(source_root)
    default_build_jobs = str(min(8, os.cpu_count() or 4))
    build_jobs = os.environ.get("ATS_URTEST_BUILD_JOBS", default_build_jobs)
    configure_command = [
        "cmake",
        "--preset",
        "urtest",
        "-B",
        str(build_root),
        "-DBUILD_TESTING=OFF",
        f"-DCMAKE_INSTALL_PREFIX={install_prefix}",
        f"-DURTEST_SANDBOX={sandbox}",
    ]
    commands = []
    cache_path = build_root / "CMakeCache.txt"
    if _cmake_cache_value(cache_path, "URTEST_SANDBOX") != str(sandbox):
        commands.append(configure_command)
    commands.extend([
        ["cmake", "--build", str(build_root), "-j", build_jobs],
        ["cmake", "--install", str(build_root)],
    ])
    try:
        for command in commands:
            is_install = command[0:2] == ["cmake", "--install"]
            result = subprocess.run(command, cwd=source_root, capture_output=is_install, text=is_install, check=False)
            if result.returncode != 0:
                if is_install:
                    print(result.stdout, end="")
                    print(result.stderr, end="", file=sys.stderr)
                return result.returncode

        wrapper = build_root / "tests" / "urtest.sh"
        result = subprocess.run([wrapper, "--no-run-in-docker", *test_arguments], cwd=wrapper.parent, check=False)
        return result.returncode
    finally:
        _copy_sandbox_artifacts(sandbox, build_root / "sandbox")
        _restore_container_artifact_ownership(source_root, build_root)


def run_configured(config: ConfiguredBuild, arguments: Sequence[str]) -> int:
    """Run pytest for every Uranium test in an already configured build."""

    should_use_docker, test_arguments = choose_docker_mode(arguments)
    if should_use_docker:
        return launch_docker(config.source_root, test_arguments)

    pytest_arguments = translate_arguments(test_arguments, os.environ)
    if any(argument in ("-h", "--help") for argument in test_arguments):
        print(runner_help(), flush=True)
    if config.curl_uds and "--curl-uds" not in pytest_arguments:
        pytest_arguments.append("--curl-uds")
    command = [
        "uv",
        "--project",
        str(config.project_directory),
        "run",
        "pytest",
        "--import-mode=importlib",
        "-p",
        "tools.uranium.plugin",
        str(config.source_root / "tests" / "tools" / "uranium" / "tests"),
        str(config.source_root / "tests" / "uranium_tests"),
        f"--ats-bin={config.install_prefix / 'bin'}",
        f"--proxy-verifier-bin={config.verifier_bin}",
        f"--build-root={config.build_root}",
        f"--sandbox={config.sandbox}",
        "-o",
        f"cache_dir={config.project_directory / '.pytest_cache'}",
        *pytest_arguments,
    ]
    environment = os.environ.copy()
    python_paths = [
        config.source_root / "tests",
        config.source_root / "tests" / "uranium_tests" / "remap",
        config.source_root / "tests" / "uranium_tests" / "remap_yaml",
        config.source_root / "tests" / "uranium_tests" / "lib",
    ]
    if existing := environment.get("PYTHONPATH"):
        environment["PYTHONPATH"] = os.pathsep.join([*(str(path) for path in python_paths), existing])
    else:
        environment["PYTHONPATH"] = os.pathsep.join(str(path) for path in python_paths)
    environment["LD_LIBRARY_PATH"] = _prepend_path(config.install_prefix / "lib", environment.get("LD_LIBRARY_PATH"))
    environment["PYTHONDONTWRITEBYTECODE"] = "1"
    for proxy_variable in ("HTTP_PROXY", "HTTPS_PROXY", "NO_PROXY", "http_proxy", "https_proxy", "no_proxy"):
        environment[proxy_variable] = ""

    result = subprocess.run(command, cwd=config.source_root, env=environment, check=False)
    return result.returncode


def runner_help() -> str:
    """Describe wrapper-owned options before pytest renders its own help."""

    return f"""usage: urtest.sh [urtest.sh options] [pytest options]

urtest.sh options (consumed before pytest; these spellings take precedence):
  -h, --help                    Show this section followed by pytest's help.
  --run-in-docker               Run in {DEFAULT_IMAGE}.
  --no-run-in-docker            Run in the current environment.

Docker is the default unless urtest.sh is already running inside the official
Fedora 44 test container. Any argument not consumed above is passed to pytest.
Common pytest options include -k for selection, -n for parallel workers, and
--collect-only/--co for listing tests; they are documented below.

pytest options (passed through after urtest.sh processing):"""


def choose_docker_mode(arguments: Sequence[str]) -> tuple[bool, list[str]]:
    """Apply explicit Docker flags, then the official-container default."""

    requested: bool | None = None
    remaining = []
    for argument in arguments:
        if argument == "--run-in-docker":
            if requested is False:
                raise RunnerError("--run-in-docker conflicts with --no-run-in-docker")
            requested = True
        elif argument == "--no-run-in-docker":
            if requested is True:
                raise RunnerError("--no-run-in-docker conflicts with --run-in-docker")
            requested = False
        else:
            remaining.append(argument)

    if requested is not None:
        return requested, remaining
    return not is_official_test_container(), remaining


def is_official_test_container(root: Path = Path("/")) -> bool:
    """Recognize Fedora 44 only when the process is also containerized."""

    if not is_container(root):
        return False
    release = read_os_release(root / "etc" / "os-release")
    return release.get("ID") == "fedora" and release.get("VERSION_ID") == "44"


def is_container(root: Path = Path("/")) -> bool:
    """Detect Docker, Podman, containerd, and Kubernetes environments."""

    if (root / ".dockerenv").exists() or (root / "run" / ".containerenv").exists():
        return True
    cgroup = root / "proc" / "1" / "cgroup"
    if not cgroup.is_file():
        return False
    try:
        content = cgroup.read_text(errors="replace").lower()
    except OSError:
        return False
    return any(marker in content for marker in ("docker", "containerd", "kubepods", "libpod"))


def read_os_release(path: Path) -> dict[str, str]:
    """Parse the simple key/value fields needed from ``os-release``."""

    values = {}
    try:
        lines = path.read_text().splitlines()
    except OSError:
        return values
    for line in lines:
        if not line or line.lstrip().startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key] = value.strip().strip("\"'")
    return values


def launch_docker(source_root: Path, arguments: Sequence[str]) -> int:
    """Run the source entry point once in the official Fedora test image."""

    docker = shutil.which("docker")
    if docker is None:
        raise RunnerError(
            "Docker is required by the default execution mode. Install Docker, run inside the Fedora 44 test "
            "container, or pass --no-run-in-docker to use the current environment.")
    image = os.environ.get("ATS_URTEST_DOCKER_IMAGE", DEFAULT_IMAGE)
    source_root = source_root.resolve()
    command = [
        docker,
        "run",
        "--rm",
        "--init",
        "--cap-add=SYS_PTRACE",
        "--network=host",
        "--volume",
        f"{source_root}:{source_root}",
        "--workdir",
        str(source_root),
        "--env",
        f"ATS_URTEST_HOST_UID={os.getuid()}",
        "--env",
        f"ATS_URTEST_HOST_GID={os.getgid()}",
        "--env",
        f"ATS_URTEST_HOST_OS={sys.platform}",
    ]
    for variable in ("ATS_URTEST_BUILD_JOBS", "RUN_CACHE_CONTENTION_TEST", "SHARD", "SHARDCNT"):
        if variable in os.environ:
            command.extend(["--env", f"{variable}={os.environ[variable]}"])
    if extra_arguments := os.environ.get("ATS_URTEST_DOCKER_ARGS"):
        command.extend(shlex.split(extra_arguments))
    command.extend([
        image,
        str(source_root / "tests" / "urtest.sh"),
        "--no-run-in-docker",
        *arguments,
    ])
    result = subprocess.run(command, check=False)
    return result.returncode


def translate_arguments(arguments: Sequence[str], environment: Mapping[str, str]) -> list[str]:
    """Add CI sharding options without reinterpreting pytest arguments."""

    translated = list(arguments)

    shard_count = _nonnegative_integer(environment.get("SHARDCNT"))
    shard_index = _nonnegative_integer(environment.get("SHARD"))
    if shard_count is not None and shard_count > 0:
        if shard_index is None or shard_index >= shard_count:
            raise RunnerError("SHARD and SHARDCNT must satisfy 0 <= SHARD < SHARDCNT")
        translated.extend(["--urtest-shard-index", str(shard_index), "--urtest-shard-count", str(shard_count)])
    return translated


def _parse_configured_build(arguments: list[str]) -> tuple[ConfiguredBuild, list[str]]:
    """Consume the fixed path arguments supplied by ``configure_file``."""

    if "--" not in arguments:
        raise RunnerError("The configured runner is missing its argument separator")
    separator = arguments.index("--")
    fixed = arguments[:separator]
    test_arguments = arguments[separator + 1:]
    values: dict[str, str] = {}
    curl_uds = False
    for argument in fixed:
        if argument == "--configured-curl-uds":
            curl_uds = True
            continue
        if not argument.startswith("--") or "=" not in argument:
            raise RunnerError(f"Invalid configured runner argument: {argument}")
        key, value = argument[2:].split("=", 1)
        values[key] = value
    required = {"source-root", "build-root", "install-prefix", "verifier-bin", "sandbox", "project-directory"}
    if missing := sorted(required - values.keys()):
        raise RunnerError("Missing configured runner values: " + ", ".join(missing))
    return ConfiguredBuild(
        source_root=Path(values["source-root"]),
        build_root=Path(values["build-root"]),
        install_prefix=Path(values["install-prefix"]),
        verifier_bin=Path(values["verifier-bin"]),
        sandbox=Path(values["sandbox"]),
        project_directory=Path(values["project-directory"]),
        curl_uds=curl_uds,
    ), test_arguments


def _nonnegative_integer(value: str | None) -> int | None:
    """Parse a nonnegative environment integer."""

    if value is None or value == "":
        return None
    try:
        parsed = int(value)
    except ValueError as error:
        raise RunnerError(f"Expected an integer, got {value!r}") from error
    if parsed < 0:
        raise RunnerError(f"Expected a nonnegative integer, got {value!r}")
    return parsed


def _prepend_path(path: Path, existing: str | None) -> str:
    """Prepend one filesystem path to an optional path-list value."""

    return str(path) if not existing else os.pathsep.join((str(path), existing))


def _short_sandbox(source_root: Path) -> Path:
    """Choose a short, deterministic path that leaves room for Unix sockets."""

    digest = hashlib.sha256(str(source_root.resolve()).encode()).hexdigest()[:8]
    return Path("/tmp") / f"ats-urtest-{digest}"


def _cmake_cache_value(cache_path: Path, key: str) -> str | None:
    """Read one value from a CMake cache without invoking configuration."""

    try:
        lines = cache_path.read_text().splitlines()
    except OSError:
        return None
    prefix = f"{key}:"
    for line in lines:
        if line.startswith(prefix) and "=" in line:
            return line.split("=", 1)[1]
    return None


def _copy_sandbox_artifacts(source: Path, destination: Path) -> None:
    """Preserve test output from container-local ``/tmp`` without its sockets."""

    if not source.is_dir() or source.resolve() == destination.resolve():
        return

    def ignore_special_files(directory: str, names: list[str]) -> list[str]:
        ignored = []
        for name in names:
            try:
                status = (Path(directory) / name).lstat()
            except OSError:
                ignored.append(name)
                continue
            if not (stat.S_ISREG(status.st_mode) or stat.S_ISDIR(status.st_mode)):
                ignored.append(name)
            elif stat.S_ISREG(status.st_mode) and status.st_size > 64 * 1024 * 1024:
                # Cache spans are reproducible inputs, not useful diagnostics.
                ignored.append(name)
        return ignored

    try:
        shutil.rmtree(destination, ignore_errors=True)
        shutil.copytree(source, destination, ignore=ignore_special_files)
    except OSError as error:
        print(f"urtest.sh: could not preserve sandbox artifacts: {error}", file=sys.stderr)


def _restore_host_ownership(root: Path) -> None:
    """Avoid leaving root-owned container build artifacts in a bind mount."""

    if os.geteuid() != 0 or not root.exists():
        return
    if os.environ.get("ATS_URTEST_HOST_OS") == "darwin":
        # Docker Desktop presents bind mounts as root-owned in the VM while
        # preserving the macOS owner's access on the host.
        return
    try:
        uid = int(os.environ.get("ATS_URTEST_HOST_UID", "0"))
        gid = int(os.environ.get("ATS_URTEST_HOST_GID", "0"))
    except ValueError:
        return
    if uid == 0 and gid == 0:
        return

    def restore(path: Path) -> None:
        try:
            status = path.stat(follow_symlinks=False)
            if status.st_uid != uid or status.st_gid != gid:
                os.chown(path, uid, gid, follow_symlinks=False)
        except OSError:
            pass

    for directory, directories, files in os.walk(root):
        for name in [*directories, *files]:
            restore(Path(directory) / name)
    restore(root)


def _restore_container_artifact_ownership(source_root: Path, build_root: Path) -> None:
    """Return bind-mounted build and Proxy Verifier caches to the host user."""

    _restore_host_ownership(build_root)
    git_directory = source_root / ".git"
    if git_directory.is_dir():
        _restore_host_ownership(git_directory / "proxy-verifier")
        for verifier_directory in git_directory.glob("proxy-verifier-*"):
            _restore_host_ownership(verifier_directory)


if __name__ == "__main__":
    raise SystemExit(main())
