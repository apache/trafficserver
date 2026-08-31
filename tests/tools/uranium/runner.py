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
CONTAINER_MARKER = "ATS_URTEST_IN_CONTAINER"


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
    """Build ATS in the official environment, then invoke the configured runner.

    :param source_root: ATS source-tree root.
    :param arguments: Wrapper and pytest arguments from the command line.
    """

    source_root = source_root.resolve()
    should_use_container, test_arguments = choose_container_mode(arguments)
    if should_use_container:
        return launch_container(source_root, test_arguments)

    build_root = Path(os.environ.get("ATS_URTEST_CONTAINER_BUILD", source_root / "build-urtest-container")).resolve()
    install_prefix = build_root / "install"
    sandbox = Path(os.environ.get("ATS_URTEST_SANDBOX", _short_sandbox(source_root))).resolve()
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
        result = subprocess.run([wrapper, "--no-run-in-container", *test_arguments], cwd=wrapper.parent, check=False)
        return result.returncode
    finally:
        _copy_sandbox_artifacts(sandbox, build_root / "sandbox")
        _restore_container_artifact_ownership(source_root, build_root)


def run_configured(config: ConfiguredBuild, arguments: Sequence[str]) -> int:
    """Run pytest for every Uranium test in an already configured build."""

    should_use_container, test_arguments = choose_container_mode(arguments)
    if should_use_container:
        return launch_container(config.source_root, test_arguments)

    pytest_arguments = translate_arguments(test_arguments, os.environ)
    if any(argument in ("-h", "--help") for argument in test_arguments):
        print(runner_help(), flush=True)
    if config.curl_uds and "--curl-uds" not in pytest_arguments:
        pytest_arguments.append("--curl-uds")
    command = [
        *_uv_run_prefix(config.project_directory, is_official_test_container()),
        "pytest",
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
    library_path_variable = _library_path_variable()
    environment[library_path_variable] = _prepend_path(config.install_prefix / "lib", environment.get(library_path_variable))
    environment["PYTHONDONTWRITEBYTECODE"] = "1"
    for proxy_variable in ("HTTP_PROXY", "HTTPS_PROXY", "NO_PROXY", "http_proxy", "https_proxy", "no_proxy"):
        environment[proxy_variable] = ""

    result = subprocess.run(command, cwd=config.source_root, env=environment, check=False)
    return result.returncode


def _uv_run_prefix(project_directory: Path, use_accelerated_diff: bool) -> list[str]:
    """Build the uv prefix used to launch pytest.

    :param project_directory: Directory containing the Uranium Python project.
    :param use_accelerated_diff: Whether to install the optional C diff extra.
    """

    command = ["uv", "--project", str(project_directory), "run", "--locked"]
    if use_accelerated_diff:
        command.extend(["--extra", "fast-diff"])
    return command


def _library_path_variable(platform: str | None = None) -> str:
    """Return the dynamic-library search variable for a host platform.

    :param platform: Host platform name, or ``None`` to use ``sys.platform``.
    :return: Environment variable honored by the platform's dynamic loader.
    """

    return "DYLD_LIBRARY_PATH" if (sys.platform if platform is None else platform) == "darwin" else "LD_LIBRARY_PATH"


def runner_help() -> str:
    """Describe wrapper-owned options before pytest renders its own help."""

    return f"""usage: urtest.sh [urtest.sh options] [pytest options]

urtest.sh options (consumed before pytest; these spellings take precedence):
  -h, --help                    Show this section followed by pytest's help.
  --run-in-container            Run in {DEFAULT_IMAGE}.
  --no-run-in-container         Run in the current environment.
  --run-in-docker               Alias for --run-in-container.
  --no-run-in-docker            Alias for --no-run-in-container.

Container execution is the default unless urtest.sh detects that it is already
running in a container or the Fedora 44 test environment. The launcher prefers
Apple container on macOS, Podman on Linux, and Docker as a fallback.
Any argument not consumed above is passed to pytest.
Common pytest options include -k for selection, -n for parallel workers, and
--collect-only/--co for listing tests. Pass --run-manual to include explicitly
opt-in Uranium tests; pytest documents these options below.

pytest options (passed through after urtest.sh processing):"""


def choose_container_mode(arguments: Sequence[str], root: Path = Path("/")) -> tuple[bool, list[str]]:
    """Apply explicit container flags, then avoid nested execution.

    :param arguments: Wrapper and pytest arguments from the command line.
    :param root: Filesystem root to inspect for container markers.
    """

    requested: bool | None = None
    remaining = []
    for argument in arguments:
        if argument in ("--run-in-container", "--run-in-docker"):
            if requested is False:
                raise RunnerError("container execution conflicts with direct execution")
            requested = True
        elif argument in ("--no-run-in-container", "--no-run-in-docker"):
            if requested is True:
                raise RunnerError("direct execution conflicts with container execution")
            requested = False
        else:
            remaining.append(argument)

    if requested is not None:
        return requested, remaining
    return not (is_container(root) or is_fedora_44(root)), remaining


def is_official_test_container(root: Path = Path("/")) -> bool:
    """Recognize the Fedora 44 test environment across container runtimes."""

    return is_fedora_44(root)


def is_fedora_44(root: Path = Path("/")) -> bool:
    """Check whether a filesystem root identifies Fedora 44.

    :param root: Filesystem root containing ``etc/os-release``.
    """

    release = read_os_release(root / "etc" / "os-release")
    return release.get("ID") == "fedora" and release.get("VERSION_ID") == "44"


def is_container(root: Path = Path("/")) -> bool:
    """Detect known OCI and orchestration container environments.

    :param root: Filesystem root to inspect for runtime markers.
    """

    if os.environ.get(CONTAINER_MARKER) == "1" or os.environ.get("container"):
        return True
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


def find_container_runtime(platform: str | None = None) -> tuple[str, str] | None:
    """Select the preferred installed container runtime for the host.

    :param platform: Host platform name, or ``None`` to use ``sys.platform``.
    :return: Runtime name and executable path, or ``None`` when unavailable.
    """

    host_platform = sys.platform if platform is None else platform
    candidates = ("container", "podman", "docker") if host_platform == "darwin" else ("podman", "docker")
    for name in candidates:
        if executable := shutil.which(name):
            return name, executable
    return None


def launch_container(source_root: Path, arguments: Sequence[str]) -> int:
    """Run the source entry point once in the official Fedora test image.

    :param source_root: ATS source-tree root to bind mount.
    :param arguments: Pytest arguments to pass through the container.
    """

    runtime = find_container_runtime()
    if runtime is None:
        raise RunnerError(
            "A supported container runtime is required by the default execution mode. Install Apple container "
            "on macOS, Podman or Docker, run inside the Fedora 44 test environment, or pass "
            "--no-run-in-container to use the current environment.")
    runtime_name, executable = runtime
    image = os.environ.get("ATS_URTEST_CONTAINER_IMAGE", os.environ.get("ATS_URTEST_DOCKER_IMAGE", DEFAULT_IMAGE))
    source_root = source_root.resolve()
    command = [
        executable,
        "run",
        "--rm",
        "--init",
        "--cap-add",
        "SYS_PTRACE",
    ]
    if runtime_name != "container":
        command.append("--network=host")
    command.extend(
        [
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
            "--env",
            f"{CONTAINER_MARKER}=1",
        ])
    for variable in ("ATS_URTEST_BUILD_JOBS", "ATS_URTEST_SANDBOX", "RUN_CACHE_CONTENTION_TEST", "SHARD", "SHARDCNT"):
        if variable in os.environ:
            command.extend(["--env", f"{variable}={os.environ[variable]}"])
    if extra_arguments := os.environ.get("ATS_URTEST_CONTAINER_ARGS", os.environ.get("ATS_URTEST_DOCKER_ARGS")):
        command.extend(shlex.split(extra_arguments))
    command.extend([
        image,
        str(source_root / "tests" / "urtest.sh"),
        "--no-run-in-container",
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
