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
"""Pytest-native orchestration for procedural ATS Uranium tests."""

from __future__ import annotations

from collections.abc import Callable, Iterable, Iterator, Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path
from string import Template
from typing import Any as TypingAny
import contextvars
import copy
import difflib
import json
import os
import re
import shlex
import shutil
import signal
import socket
import ssl
import subprocess
import sys
import tempfile
import time

import pytest
import yaml

from .config import merge_flat_records
from .runtime import TestRuntime


class ScenarioError(RuntimeError):
    """Report a pytest-native Uranium scenario failure."""


class Namespace(dict[str, TypingAny]):
    """Provide dictionary and attribute access for test runtime values."""

    def __getattr__(self, name: str) -> TypingAny:
        try:
            return self[name]
        except KeyError as error:
            raise AttributeError(name) from error

    def __setattr__(self, name: str, value: TypingAny) -> None:
        self[name] = value


@dataclass(frozen=True)
class Choice:
    """Represent a set of acceptable values or alternatives."""

    values: tuple[TypingAny, ...]


def Any(*values: TypingAny) -> Choice:  # noqa: N802 - retained while test sources migrate
    """Accept any of @a values."""

    return Choice(values)


@dataclass(frozen=True)
class Condition:
    """A lazily evaluated prerequisite for a pytest test."""

    predicate: Callable[[], bool]
    reason: str
    expected: bool = True

    def __call__(self) -> bool:
        return bool(self.predicate()) is self.expected

    @classmethod
    def true(cls, value: TypingAny) -> "Condition":
        return cls(lambda: bool(value), f"Expected {value!r} to be true")

    @classmethod
    def HasProgram(cls, program: str, reason: str | None = None) -> "Condition":  # noqa: N802
        return cls(lambda: shutil.which(program) is not None, reason or f"{program} is required")

    @classmethod
    def PluginExists(cls, plugin: str) -> "Condition":  # noqa: N802
        scenario = _scenario()
        name = plugin if plugin.endswith(".so") else f"{plugin}.so"
        path = Path(scenario.runtime.layout["PLUGINDIR"]) / name
        return cls(path.is_file, f"ATS plugin is not installed: {name}")

    @classmethod
    def HasATSFeature(cls, feature: str) -> "Condition":  # noqa: N802
        scenario = _scenario()
        return cls(lambda: bool(scenario.runtime.features.get(feature)), f"ATS was built without {feature}")

    @classmethod
    def HasCurlFeature(cls, feature: str) -> "Condition":  # noqa: N802
        return cls(lambda: feature.lower() in _command_output(["curl", "--version"]).lower(), f"curl lacks {feature}")

    @classmethod
    def HasCurlOption(cls, option: str) -> "Condition":  # noqa: N802
        return cls(lambda: option in _command_output(["curl", "--help", "all"]), f"curl lacks {option}")

    @classmethod
    def HasCurlVersion(cls, version: str) -> "Condition":  # noqa: N802
        return cls(lambda: _version(_command_output(["curl", "--version"])) >= _version(version), f"curl {version} is required")

    @classmethod
    def HasProxyVerifierVersion(cls, version: str) -> "Condition":  # noqa: N802
        scenario = _scenario()
        binary = scenario.runtime.verifier_bin / "verifier-client"
        return cls(
            lambda: _version(_command_output([str(binary), "--version"])) >= _version(version),
            f"Proxy Verifier {version} is required")

    @classmethod
    def HasOpenSSLVersion(cls, version: str) -> "Condition":  # noqa: N802
        return cls(lambda: _version(_command_output(["openssl", "version"])) >= _version(version), f"OpenSSL {version} is required")

    @classmethod
    def HasOpenSSLQuicClient(cls) -> "Condition":  # noqa: N802
        return cls(lambda: "-quic" in _command_output(["openssl", "s_client", "-help"]), "OpenSSL CLI must support s_client -quic")

    @classmethod
    def IsPlatform(cls, platform: str) -> "Condition":  # noqa: N802
        return cls(lambda: sys.platform.startswith(platform.lower()), f"Test requires {platform}")

    @classmethod
    def IsOpenSSL(cls) -> "Condition":  # noqa: N802
        return cls(lambda: "boringssl" not in _command_output(["openssl", "version"]).lower(), "Test requires OpenSSL")

    @classmethod
    def IsBoringSSL(cls) -> "Condition":  # noqa: N802
        return cls(lambda: "boringssl" in _command_output(["openssl", "version"]).lower(), "Test requires BoringSSL")

    @classmethod
    def CurlUsingUnixDomainSocket(cls) -> "Condition":  # noqa: N802
        scenario = _scenario()
        return cls(lambda: bool(scenario.Variables.get("CurlUds", False)), "Test requires curl over a Unix socket")

    @classmethod
    def HasLegacyTLSSupport(cls, *_args: TypingAny, **_kwargs: TypingAny) -> "Condition":  # noqa: N802
        return cls(lambda: "tlsv1" in _command_output(["openssl", "ciphers", "-v"]).lower(), "Legacy TLS is unavailable")

    @classmethod
    def HasCurlTLSVersionSupport(cls, version: str) -> "Condition":  # noqa: N802
        option = "--tlsv" + version.replace(".", ".")
        return cls(lambda: option in _command_output(["curl", "--help", "all"]), f"curl lacks TLS {version}")

    @classmethod
    def HasGoVersion(cls, version: str) -> "Condition":  # noqa: N802
        return cls(lambda: _version(_command_output(["go", "version"])) >= _version(version), f"Go {version} is required")


def _condition_all(*conditions: TypingAny) -> Condition:
    normalized = [_as_condition(condition) for condition in conditions]
    return Condition(lambda: all(condition() for condition in normalized), "; ".join(condition.reason for condition in normalized))


def _as_condition(value: TypingAny) -> Condition:
    if isinstance(value, Condition):
        return value
    if isinstance(value, Choice):
        normalized = [_as_condition(item) for item in value.values]
        return Condition(lambda: any(item() for item in normalized), "; ".join(item.reason for item in normalized))
    if callable(value):
        return Condition(value, "condition was not satisfied")
    return Condition(lambda: bool(value), f"Expected {value!r} to be true")


class ReadyCheck:
    """Poll a process-readiness predicate."""

    def __init__(self, predicate: Callable[[], bool], description: str) -> None:
        self.predicate = predicate
        self.description = description

    def __call__(self) -> bool:
        try:
            return bool(self.predicate())
        except (OSError, ValueError):
            return False


class When:
    """Factories for process-readiness predicates."""

    @staticmethod
    def PortOpen(port: int, address: str = "127.0.0.1") -> ReadyCheck:  # noqa: N802
        return ReadyCheck(lambda: _port_open(address, int(port)), f"{address}:{port} to accept connections")

    PortOpenv4 = PortOpen

    @staticmethod
    def PortOpenv6(port: int, address: str = "::1") -> ReadyCheck:  # noqa: N802
        return ReadyCheck(lambda: _port_open(address, int(port)), f"[{address}]:{port} to accept connections")

    @staticmethod
    def PortReady(port: int, address: str = "127.0.0.1") -> ReadyCheck:  # noqa: N802
        return When.PortOpen(port, address)

    @staticmethod
    def FileExists(path: str | Path) -> ReadyCheck:  # noqa: N802
        return ReadyCheck(Path(path).exists, f"{path} to exist")

    @staticmethod
    def FileContains(path: str | Path, expression: str, desired_count: int = 1) -> ReadyCheck:  # noqa: N802
        target = Path(path)

        def contains() -> bool:
            if not target.is_file():
                return False
            return len(re.findall(expression, target.read_text(errors="replace"), re.MULTILINE)) >= desired_count

        return ReadyCheck(contains, f"{path} to contain {expression!r} {desired_count} time(s)")

    @staticmethod
    def ReloadCompleted(*_args: TypingAny, **_kwargs: TypingAny) -> ReadyCheck:  # noqa: N802
        return ReadyCheck(lambda: True, "configuration reload to complete")


class Validator:
    """Validate captured output or a declared file."""

    def validate(self, text: str, path: Path | None, test_directory: Path) -> None:
        raise NotImplementedError

    def __add__(self, other: TypingAny) -> "ValidatorGroup":
        return ValidatorGroup([self]) + other


class ValidatorGroup(Validator):
    """Apply every contained output validator."""

    def __init__(self, validators: Iterable[TypingAny] = ()) -> None:
        self.validators: list[TypingAny] = []
        for validator in validators:
            self += validator

    def __iadd__(self, validator: TypingAny) -> "ValidatorGroup":
        if validator is None:
            return self
        if isinstance(validator, ValidatorGroup):
            self.validators.extend(validator.validators)
        elif isinstance(validator, list):
            self.validators.extend(validator)
        else:
            self.validators.append(validator)
        return self

    def __add__(self, validator: TypingAny) -> "ValidatorGroup":
        result = ValidatorGroup(self.validators)
        result += validator
        return result

    def validate(self, text: str, path: Path | None, test_directory: Path) -> None:
        for validator in self.validators:
            _validate(validator, text, path, test_directory)


def All(*validators: TypingAny) -> ValidatorGroup:  # noqa: N802 - retained while test sources migrate
    """Require all provided validators."""

    return ValidatorGroup(validators)


class ExpressionValidator(Validator):

    def __init__(self, expression: str, description: str = "", *, present: bool, literal: bool = False, reflags: int = 0) -> None:
        self.expression = expression
        self.description = description or expression
        self.present = present
        self.literal = literal
        self.reflags = reflags

    def validate(self, text: str, path: Path | None, test_directory: Path) -> None:
        found = self.expression in text if self.literal else re.search(
            self.expression, text, self.reflags | re.MULTILINE) is not None
        if found is not self.present:
            expectation = "contain" if self.present else "exclude"
            raise AssertionError(f"Expected {path or 'process output'} to {expectation} {self.expression!r}: {self.description}")


class GoldValidator(Validator):

    def __init__(self, expected: str | Path) -> None:
        self.expected = Path(expected)

    def validate(self, text: str, path: Path | None, test_directory: Path) -> None:
        expected = self.expected if self.expected.is_absolute() else test_directory / self.expected
        expected_text = expected.read_text(errors="replace")
        pattern = re.escape(expected_text).replace(re.escape("``"), ".*?").replace(re.escape("{}"), ".*?")
        if re.fullmatch(pattern, text, re.DOTALL) is None:
            difference = "".join(
                difflib.unified_diff(
                    expected_text.splitlines(True), text.splitlines(True), fromfile=str(expected), tofile=str(path or "output")))
            raise AssertionError(f"{path or 'process output'} did not match {expected}:\n{difference}")


class LambdaValidator(Validator):

    def __init__(self, function: Callable[..., TypingAny], description: str = "") -> None:
        self.function = function
        self.description = description or getattr(function, "__name__", "lambda validator")

    def validate(self, text: str, path: Path | None, test_directory: Path) -> None:
        info = Namespace(AbsPath=str(path) if path else "", Content=text)
        try:
            result = self.function(info, self)
        except TypeError:
            result = self.function(info)
        if isinstance(result, tuple):
            passed, *reason = result
            if not passed:
                raise AssertionError(str(reason[0] if reason else self.description))
        elif result is False:
            raise AssertionError(self.description)


class CallbackValidator(Validator):

    def __init__(self, function: Callable[[Path], TypingAny], description: str = "") -> None:
        self.function = function
        self.description = description or getattr(function, "__name__", "file callback")

    def validate(self, text: str, path: Path | None, test_directory: Path) -> None:
        result = self.function(path)
        if isinstance(result, tuple) and not result[0]:
            raise AssertionError(str(result[1] if len(result) > 1 else self.description))
        if result is False:
            raise AssertionError(self.description)


class JSONRPCValidator(Validator):

    def __init__(self, function: Callable[[TypingAny], tuple[bool, str]]) -> None:
        self.function = function

    def validate(self, text: str, path: Path | None, test_directory: Path) -> None:
        try:
            from jsonrpc import Response
            response = Response(text=text)
        except (ImportError, TypeError):
            response = json.loads(text)
        passed, reason = self.function(response)
        if not passed:
            raise AssertionError(reason)


class Testers:
    """Factories for pytest-native output assertions."""

    @staticmethod
    def ContainsExpression(
            expression: str, description: str = "", reflags: int = 0, **_kwargs: TypingAny) -> Validator:  # noqa: N802
        return ExpressionValidator(expression, description, present=True, reflags=reflags)

    @staticmethod
    def ExcludesExpression(expression: str, description: str = "", **_kwargs: TypingAny) -> Validator:  # noqa: N802
        return ExpressionValidator(expression, description, present=False)

    @staticmethod
    def IncludesExpression(expression: str, description: str = "", **_kwargs: TypingAny) -> Validator:  # noqa: N802
        return ExpressionValidator(expression, description, present=True, literal=True)

    @staticmethod
    def GoldFile(path: str | Path, *_args: TypingAny, **_kwargs: TypingAny) -> Validator:  # noqa: N802
        return GoldValidator(path)

    @staticmethod
    def Lambda(function: Callable[..., TypingAny], description: str = "", **_kwargs: TypingAny) -> Validator:  # noqa: N802
        return LambdaValidator(function, description)

    @staticmethod
    def FileContentCallback(function: Callable[[Path], TypingAny], description: str = "") -> Validator:  # noqa: N802
        return CallbackValidator(function, description)

    @staticmethod
    def CustomJSONRPCResponse(function: Callable[[TypingAny], tuple[bool, str]], **_kwargs: TypingAny) -> Validator:  # noqa: N802
        return JSONRPCValidator(function)

    @staticmethod
    def GreaterThan(value: float, description: str = "") -> Validator:  # noqa: N802
        return LambdaValidator(lambda info, _tester: (float(info.Content.strip()) > value, description))

    CurlHeader = GoldFile
    JSONRPCResponseSchemaValidator = GoldFile


class OutputTarget:
    """A captured stream or file with deferred assertions."""

    def __init__(self, path: Path | None = None, combined: Sequence["OutputTarget"] = ()) -> None:
        self.path = path
        self.combined = tuple(combined)
        self.Content: TypingAny = ValidatorGroup()

    @property
    def Name(self) -> str:
        return str(self.path or "")

    @property
    def AbsPath(self) -> str:
        return self.Name

    def read(self) -> str:
        if self.combined:
            return "".join(target.read() for target in self.combined)
        return self.path.read_text(errors="replace") if self.path and self.path.is_file() else ""

    def __iadd__(self, validator: TypingAny) -> "OutputTarget":
        if not isinstance(self.Content, ValidatorGroup):
            self.Content = ValidatorGroup([self.Content])
        self.Content += validator
        return self

    def validate(self, test_directory: Path) -> None:
        _validate(self.Content, self.read(), self.path, test_directory)


class Streams:
    """Captured stdout and stderr targets for one process."""

    def __init__(self, directory: Path, name: str) -> None:
        self._stdout = OutputTarget(directory / f"{name}.stdout")
        self._stderr = OutputTarget(directory / f"{name}.stderr")
        self._all = OutputTarget(combined=(self._stdout, self._stderr))

    @property
    def stdout(self) -> OutputTarget:
        return self._stdout

    @stdout.setter
    def stdout(self, value: TypingAny) -> None:
        if value is not self._stdout:
            self._stdout.Content = value

    @property
    def stderr(self) -> OutputTarget:
        return self._stderr

    @stderr.setter
    def stderr(self, value: TypingAny) -> None:
        if value is not self._stderr:
            self._stderr.Content = value

    @property
    def All(self) -> OutputTarget:  # noqa: N802
        return self._all

    @All.setter
    def All(self, value: TypingAny) -> None:  # noqa: N802
        if value is not self._all:
            self._all.Content = value

    @property
    def all(self) -> OutputTarget:
        return self._all

    @all.setter
    def all(self, value: TypingAny) -> None:
        if value is not self._all:
            self._all.Content = value


class FileNode(OutputTarget):
    """A file staged before execution or asserted afterward."""

    def __init__(self, path: Path, *, exists: bool | None = None, typename: str | None = None) -> None:
        super().__init__(path)
        self.Exists = exists
        self.typename = typename or ""
        self.content: TypingAny = None
        self._lines: list[str] = []
        self._documents: list[dict[str, TypingAny]] = [{}]
        self._write_callbacks: list[Callable[[str], TypingAny]] = []

    def AddLine(self, line: TypingAny) -> "FileNode":  # noqa: N802
        self._lines.append(str(line).rstrip("\n") + "\n")
        return self

    def AddLines(self, lines: Iterable[TypingAny] | str) -> "FileNode":  # noqa: N802
        values = lines.splitlines() if isinstance(lines, str) else lines
        for line in values:
            self.AddLine(line)
        return self

    def WriteOn(self, content: str) -> "FileNode":  # noqa: N802
        self.content = content
        return self

    def WriteCustomOn(self, callback: Callable[[str], TypingAny]) -> "FileNode":  # noqa: N802
        self._write_callbacks.append(callback)
        return self

    def update(self, values: Mapping[str, TypingAny] | str) -> None:
        parsed = yaml.safe_load(values) if isinstance(values, str) else copy.deepcopy(dict(values))
        if "records" in self.typename:
            parsed = merge_flat_records(parsed)["records"]
        _deep_update(self._documents[0], parsed or {})

    def append_to_document(self, values: Mapping[str, TypingAny] | str) -> None:
        parsed = yaml.safe_load(values) if isinstance(values, str) else copy.deepcopy(dict(values))
        if "records" in self.typename:
            parsed = merge_flat_records(parsed)["records"]
        document: dict[str, TypingAny] = {}
        _deep_update(document, parsed or {})
        self._documents.append(document)

    def materialize(self) -> None:
        path = Path(self.Name)
        path.parent.mkdir(parents=True, exist_ok=True)
        if self.content is not None:
            path.write_text(str(self.content))
        elif self._lines:
            path.write_text("".join(self._lines))
        elif any(self._documents):
            documents: list[TypingAny] = self._documents
            if "records" in self.typename:
                documents = [{"records": document} for document in self._documents]
            path.write_text(yaml.safe_dump_all(documents, sort_keys=False))
        for callback in self._write_callbacks:
            callback(str(path))

    def validate(self, test_directory: Path) -> None:
        path = Path(self.Name)
        if self.Exists is not None and path.exists() is not self.Exists:
            raise AssertionError(f"Expected {path} existence to be {self.Exists}")
        super().validate(test_directory)


class Disk:
    """Registry of files associated with a scenario entity."""

    def __init__(self, owner: "Entity") -> None:
        self.owner = owner
        self._files: list[FileNode] = []

    def File(
            self,
            path: str | Path,
            id: str | None = None,
            exists: bool | None = None,  # noqa: A002, N802
            typename: str | None = None,
            content: TypingAny = None,
            **_kwargs: TypingAny) -> FileNode:
        node = FileNode(Path(path), exists=exists, typename=typename)
        if content is not None:
            node.Content = content
        self._files.append(node)
        if id:
            setattr(self, id, node)
        return node

    def Directory(self, path: str | Path, exists: bool | None = None, **_kwargs: TypingAny) -> FileNode:  # noqa: N802
        return self.File(path, exists=exists)

    def MakeConfigFile(self, name: str) -> FileNode:  # noqa: N802
        config = Path(self.owner.Variables.CONFIGDIR) / name
        return self.File(
            config, id=_identifier(name), typename="ats:config:yaml" if name.endswith((".yaml", ".yml")) else "ats:config")

    def materialize(self) -> None:
        for node in self._files:
            node.materialize()

    def validate(self, test_directory: Path) -> None:
        for node in self._files:
            node.validate(test_directory)


class Setup:
    """Immediate filesystem setup helpers scoped to one pytest sandbox."""

    def __init__(self, owner: "Entity") -> None:
        self.owner = owner
        self._actions: list[Callable[[], None]] = []
        self._was_applied = False

    def MakeDir(self, path: str | Path) -> None:  # noqa: N802
        Path(path).mkdir(parents=True, exist_ok=True)

    def Copy(self, source: str | Path, destination: str | Path | None = None, *_args: TypingAny) -> None:  # noqa: N802
        self._actions.append(lambda: self._copy(source, destination, preserve_name=True))

    def CopyAs(self, source: str | Path, destination: str | Path | None = None, *_args: TypingAny) -> None:  # noqa: N802
        self._actions.append(lambda: self._copy(source, destination, preserve_name=False))

    def Chown(self, path: str | Path, *_args: TypingAny, **_kwargs: TypingAny) -> None:  # noqa: N802

        def chown() -> None:
            try:
                shutil.chown(path, user="nobody")
            except (LookupError, OSError):
                pass

        self._actions.append(chown)

    def Lambda(self, function: Callable[..., TypingAny] | None = None, *args: TypingAny, **kwargs: TypingAny) -> None:  # noqa: N802
        callback = function or kwargs.pop("func", None)
        if callback is not None:
            self._actions.append(lambda: callback(*args, **kwargs))

    def apply(self) -> None:
        if self._was_applied:
            return
        self._was_applied = True
        for action in self._actions:
            action()

    def _copy(self, source: str | Path, destination: str | Path | None, *, preserve_name: bool) -> None:
        source_path = self.owner.resolve_path(source)
        destination_path = Path(destination) if destination is not None else Path(self.owner.RunDirectory)
        if not destination_path.is_absolute():
            destination_path = Path(self.owner.RunDirectory) / destination_path
        if destination_path.is_dir() or destination is None or str(destination).endswith(os.sep):
            destination_path /= source_path.name
        elif preserve_name and not destination_path.suffix and not destination_path.exists():
            destination_path.mkdir(parents=True, exist_ok=True)
            destination_path /= source_path.name
        destination_path.parent.mkdir(parents=True, exist_ok=True)
        if source_path.is_dir():
            target = destination_path / source_path.name if preserve_name and destination_path.exists() and destination_path.is_dir(
            ) else destination_path
            shutil.copytree(source_path, target, dirs_exist_ok=True, symlinks=True)
        else:
            shutil.copy2(source_path, destination_path)


class Entity:
    """Shared paths and helpers for tests, runs, and processes."""

    def __init__(self, scenario: "UraniumTest", name: str, run_directory: Path) -> None:
        self.scenario = scenario
        self.Name = name
        self.RunDirectory = str(run_directory)
        self.TestDirectory = str(scenario.test_directory)
        self.Variables = Namespace(scenario.Variables)
        self.Setup = Setup(self)
        self.Disk = Disk(self)

    def resolve_path(self, value: str | Path) -> Path:
        text = str(value)
        text = re.sub(r"\{([A-Za-z_][A-Za-z0-9_]*)\}", lambda match: str(self.Variables.get(match.group(1), match.group(0))), text)
        path = Path(os.path.expandvars(text))
        if path.is_absolute():
            return path
        candidates = [
            Path(self.TestDirectory) / path,
            Path(self.RunDirectory) / path, self.scenario.runtime.build_uranium_tests / path
        ]
        return next((candidate for candidate in candidates if candidate.exists()), candidates[0])


class Process(Entity):
    """One subprocess configured and executed by pytest."""

    def __init__(
            self,
            scenario: "UraniumTest",
            name: str,
            run_directory: Path,
            command: str = "true",
            return_code: TypingAny = 0) -> None:
        directory = run_directory / name
        directory.mkdir(parents=True, exist_ok=True)
        super().__init__(scenario, name, directory)
        self.Command = command
        self.ReturnCode = return_code
        self.Ready: ReadyCheck | Callable[[], bool] | None = None
        self.StartupTimeout = 10.0
        self.TimeOut = 60.0
        self.Timeout = 60.0
        self.DelayStart = 0.0
        self.Env: dict[str, str] = {}
        self.Streams = Streams(directory, name)
        self.dependencies: list[Process] = []
        self._process: subprocess.Popen[bytes] | None = None
        self._stdout: TypingAny = None
        self._stderr: TypingAny = None
        self._started = False
        self._validated = False

    def ComposeEnv(self) -> dict[str, str]:  # noqa: N802
        environment = os.environ.copy()
        environment.update({key: str(value) for key, value in self.Env.items()})
        return environment

    def __add__(self, other: TypingAny) -> "ProcessGroup":
        return ProcessGroup([self]) + other

    def __iadd__(self, other: TypingAny) -> "ProcessGroup":
        return self + other

    def StartBefore(self, *processes: "Process", ready: ReadyCheck | Callable[[], bool] | None = None) -> "Process":  # noqa: N802
        for process in processes:
            if ready is not None:
                process.Ready = ready
            if process not in self.dependencies:
                self.dependencies.append(process)
        return self

    def StartAfter(self, *processes: "Process", ready: ReadyCheck | Callable[[], bool] | None = None) -> "Process":  # noqa: N802
        for process in processes:
            if ready is not None:
                self.Ready = ready
            if self not in process.dependencies:
                process.dependencies.append(self)
        return self

    @property
    def return_code(self) -> int | None:
        return self._process.poll() if self._process else None

    def is_running(self) -> bool:
        return self._process is not None and self._process.poll() is None

    def start(self) -> None:
        if self.is_running():
            return
        for dependency in self.dependencies:
            dependency.start()
        if self.DelayStart:
            time.sleep(float(self.DelayStart))
        self.Setup.apply()
        self.Disk.materialize()
        command = self.Command or "true"
        self._stdout = Path(self.Streams.stdout.Name).open("wb")
        self._stderr = Path(self.Streams.stderr.Name).open("wb")
        self._process = subprocess.Popen(
            ["/bin/bash", "-o", "pipefail", "-c", command],
            cwd=self.RunDirectory,
            env=self.ComposeEnv(),
            stdout=self._stdout,
            stderr=self._stderr,
            start_new_session=True,
        )
        self._started = True
        if self.Ready is not None:
            self._wait_ready()

    def wait(self) -> None:
        if self._process is None:
            raise ScenarioError(f"{self.Name} was not started")
        timeout = float(self.TimeOut if self.TimeOut is not None else self.Timeout)
        try:
            code = self._process.wait(timeout=timeout)
        except subprocess.TimeoutExpired as error:
            self.stop()
            raise ScenarioError(f"{self.Name} timed out after {timeout:g}s\n{self.output()}") from error
        finally:
            self._close_streams()
        if not _return_code_matches(code, self.ReturnCode):
            raise ScenarioError(f"{self.Name} exited with {code}; expected {self.ReturnCode!r}\n{self.output()}")

    def validate(self) -> None:
        if self._validated:
            return
        self._close_streams()
        self.Streams.stdout.validate(self.scenario.test_directory)
        self.Streams.stderr.validate(self.scenario.test_directory)
        self.Streams.All.validate(self.scenario.test_directory)
        self.Disk.validate(self.scenario.test_directory)
        self._validated = True

    def stop(self) -> None:
        if self._process and self._process.poll() is None:
            try:
                os.killpg(self._process.pid, signal.SIGTERM)
                self._process.wait(timeout=5)
            except ProcessLookupError:
                pass
            except subprocess.TimeoutExpired:
                os.killpg(self._process.pid, signal.SIGKILL)
                self._process.wait(timeout=5)
        self._close_streams()

    def output(self) -> str:
        self._flush_streams()
        return self.Streams.All.read()

    def _wait_ready(self) -> None:
        assert self._process is not None
        deadline = time.monotonic() + float(self.StartupTimeout)
        description = getattr(self.Ready, "description", "readiness condition")
        while time.monotonic() < deadline:
            code = self._process.poll()
            if code is not None:
                self._close_streams()
                if _return_code_matches(code, self.ReturnCode) and self.Ready():
                    return
                raise ScenarioError(f"{self.Name} exited with {code} while waiting for {description}\n{self.output()}")
            if self.Ready():
                return
            time.sleep(0.05)
        raise ScenarioError(f"Timed out waiting for {self.Name}: {description}\n{self.output()}")

    def _flush_streams(self) -> None:
        for stream in (self._stdout, self._stderr):
            if stream is not None and not stream.closed:
                stream.flush()

    def _close_streams(self) -> None:
        for stream in (self._stdout, self._stderr):
            if stream is not None and not stream.closed:
                stream.close()


class ProcessGroup(list[Process]):
    """Support additive process sets used by persistent-run declarations."""

    def __add__(self, value: TypingAny) -> "ProcessGroup":
        result = ProcessGroup(self)
        result += value
        return result

    def __iadd__(self, value: TypingAny) -> "ProcessGroup":
        if isinstance(value, Process):
            if value not in self:
                self.append(value)
        elif isinstance(value, (list, tuple, set, ProcessGroup)):
            for process in value:
                self += process
        return self


class ProcessRegistry:
    """Create and expose named processes for a test or run."""

    def __init__(self, owner: "UraniumTest | TestRun", create_default: bool) -> None:
        self.owner = owner
        self._processes: dict[str, Process] = {}
        if create_default:
            self.Default = self.Process("default")

    def Process(
            self, name: str, cmdstr: str | None = None, returncode: TypingAny = 0, **kwargs: TypingAny) -> Process:  # noqa: N802
        if name in self._processes:
            return self._processes[name]
        process = Process(
            self.owner.scenario if isinstance(self.owner, TestRun) else self.owner, name, Path(self.owner.RunDirectory), cmdstr or
            kwargs.get("command", "true"), returncode)
        self._processes[name] = process
        setattr(self, name, process)
        self.owner.scenario._register_process(process)
        return process

    def __iter__(self) -> Iterator[Process]:
        return iter(self._processes.values())


class TestRun(Entity):
    """A sequential pytest scenario step containing one or more processes."""

    def __init__(self, scenario: "UraniumTest", name: str, index: int) -> None:
        directory = scenario.sandbox / f"run-{index:03d}"
        directory.mkdir(parents=True, exist_ok=True)
        super().__init__(scenario, name, directory)
        self.Processes = ProcessRegistry(self, create_default=True)
        self.StillRunningAfter: TypingAny = None
        self.StillRunningBefore: TypingAny = None
        self.DelayStart = 0.0
        self.ContinueOnFail = False

    @property
    def Command(self) -> str:
        return self.Processes.Default.Command

    @Command.setter
    def Command(self, value: str) -> None:
        self.Processes.Default.Command = value

    @property
    def ReturnCode(self) -> TypingAny:
        return self.Processes.Default.ReturnCode

    @ReturnCode.setter
    def ReturnCode(self, value: TypingAny) -> None:
        self.Processes.Default.ReturnCode = value

    @property
    def Streams(self) -> Streams:
        return self.Processes.Default.Streams

    @property
    def Env(self) -> dict[str, str]:
        return self.Processes.Default.Env

    @Env.setter
    def Env(self, value: Mapping[str, str]) -> None:
        self.Processes.Default.Env = dict(value)

    @property
    def TimeOut(self) -> float:
        return self.Processes.Default.TimeOut

    @TimeOut.setter
    def TimeOut(self, value: float) -> None:
        self.Processes.Default.TimeOut = value

    def MakeCurlCommand(self, command: str, ts: Process | None = None, p: Process | None = None) -> Process:  # noqa: N802
        process = p or self.Processes.Default
        if self.scenario.Variables.get("CurlUds", False) and ts is not None:
            process.Command = f"curl --unix-socket {shlex.quote(str(ts.Variables.uds_path))} {command}"
        else:
            process.Command = f"curl {command}"
        return process

    def MakeCurlCommandMulti(self, command: str, ts: Process | None = None) -> Process:  # noqa: N802
        curl = "curl"
        if self.scenario.Variables.get("CurlUds", False) and ts is not None:
            curl = f"curl --unix-socket {shlex.quote(str(ts.Variables.uds_path))}"
        self.Processes.Default.Command = command.format(curl=curl, curl_base="curl")
        return self.Processes.Default

    def SpawnCurlCommands(
            self,
            command: str,
            count: int,
            ts: Process | None = None,
            retcode: TypingAny = 0,  # noqa: N802
            use_default: bool = True) -> list[Process]:
        processes = []
        for index in range(int(count) - (1 if use_default else 0)):
            process = self.Processes.Process(f"curl-{index}", returncode=retcode)
            self.MakeCurlCommand(command, ts, process)
            processes.append(process)
        if use_default:
            self.MakeCurlCommand(command, ts)
            self.Processes.Default.ReturnCode = retcode
            self.Processes.Default.StartBefore(*processes)
        return processes

    SpawnCommands = SpawnCurlCommands

    def MakeATSProcess(self, name: str, **kwargs: TypingAny) -> Process:  # noqa: N802
        return self.scenario._make_ats_process(self.Processes, self, name, **kwargs)

    def MakeDNServer(self, name: str, **kwargs: TypingAny) -> Process:  # noqa: N802
        return self.scenario._make_dns(self.Processes, self, name, **kwargs)

    MakeDNS = MakeDNServer

    def MakeHttpBinServer(self, name: str, **kwargs: TypingAny) -> Process:  # noqa: N802
        return self.scenario._make_httpbin(self.Processes, self, name, **kwargs)

    def AddVerifierServerProcess(self, name: str, replay_path: str | Path, **kwargs: TypingAny) -> Process:  # noqa: N802
        server = self.scenario._make_verifier_server(self.Processes, self, name, replay_path, **kwargs)
        self.Processes.Default.StartBefore(server)
        return server

    def AddVerifierClientProcess(self, name: str, replay_path: str | Path, **kwargs: TypingAny) -> Process:  # noqa: N802
        return self.scenario._configure_verifier_client(self.Processes.Default, self, name, replay_path, **kwargs)

    def AddJsonRPCClientRequest(
            self,
            ts: Process,
            request: TypingAny = "",
            file: str | None = None,
            **_kwargs: TypingAny) -> Process:  # noqa: A002, N802
        process = self.Processes.Default
        request_path = Path(file) if file else Path(process.RunDirectory) / f"request-{self.scenario._request_counter}.json"
        self.scenario._request_counter += 1
        if file is None:
            request_path.write_text(str(request))
        process.Command = f"{shlex.quote(str(ts.Variables.BINDIR))}/traffic_ctl rpc file {shlex.quote(str(request_path))} " \
                          f"--run-root {shlex.quote(ts.Disk.runroot_yaml.Name)} --format json"
        process.ReturnCode = 0
        process.Env = dict(ts.Env)
        return process

    def AddJsonRPCShowRegisterHandlerRequest(self, ts: Process) -> Process:  # noqa: N802
        from jsonrpc import Request
        return self.AddJsonRPCClientRequest(ts, Request.show_registered_handlers())


_active_scenario: contextvars.ContextVar[UraniumTest | None] = contextvars.ContextVar("active_uranium_scenario", default=None)


class UraniumTest(Entity):
    """Build and execute one procedural Uranium test inside pytest."""

    def __init__(self, runtime: TestRuntime, nodeid: str, test_path: Path, *, curl_uds: bool = False) -> None:
        self.runtime = runtime
        self.test_path = test_path
        self.test_directory = test_path.parent
        self.sandbox = runtime.item_sandbox(test_path, nodeid)
        runtime.prepare_sandbox(self.sandbox)
        self.scenario = self
        self.Variables = Namespace(
            RepoDir=str(runtime.repository_root),
            BINDIR=runtime.layout["BINDIR"],
            PREFIX=runtime.layout["PREFIX"],
            SYSCONFDIR=runtime.layout["SYSCONFDIR"],
            PLUGINDIR=runtime.layout["PLUGINDIR"],
            AtsTestToolsDir=str(runtime.test_tools),
            AtsTestPluginsDir=str(runtime.test_plugins),
            AtsBuildUraniumTestsDir=str(runtime.build_uranium_tests),
            CurlUds=curl_uds,
            Autest=Namespace(Process=Namespace(TimeOut=60)),
        )
        self.Name = test_path.stem
        self.RunDirectory = str(self.sandbox)
        self.TestDirectory = str(self.test_directory)
        self.Setup = Setup(self)
        self.Disk = Disk(self)
        self.Processes = ProcessRegistry(self, create_default=False)
        self.Summary = ""
        self.ContinueOnFail = False
        self._runs: list[TestRun] = []
        self._all_processes: list[Process] = []
        self._executed = False
        self._request_counter = 0
        self._context_token = _active_scenario.set(self)

    def close_context(self) -> None:
        try:
            _active_scenario.reset(self._context_token)
        except ValueError:
            pass

    def AddTestRun(self, name: str = "") -> TestRun:  # noqa: N802
        run = TestRun(self, name or f"run {len(self._runs) + 1}", len(self._runs))
        self._runs.append(run)
        return run

    def MakeATSProcess(self, name: str, **kwargs: TypingAny) -> Process:  # noqa: N802
        return self._make_ats_process(self.Processes, self, name, **kwargs)

    def MakeOriginServer(self, name: str, **kwargs: TypingAny) -> Process:  # noqa: N802
        return self._make_origin(self.Processes, self, name, **kwargs)

    MakeOrigin = MakeOriginServer

    def MakeDNServer(self, name: str, **kwargs: TypingAny) -> Process:  # noqa: N802
        return self._make_dns(self.Processes, self, name, **kwargs)

    MakeDNS = MakeDNServer

    def MakeHttpBinServer(self, name: str, **kwargs: TypingAny) -> Process:  # noqa: N802
        return self._make_httpbin(self.Processes, self, name, **kwargs)

    def MakeVerifierServerProcess(self, name: str, replay_path: str | Path, **kwargs: TypingAny) -> Process:  # noqa: N802
        return self._make_verifier_server(self.Processes, self, name, replay_path, **kwargs)

    def MakeCurlCommand(self, command: str, ts: Process | None = None, p: Process | None = None) -> Process:  # noqa: N802
        if not self._runs:
            self.AddTestRun("curl")
        return self._runs[-1].MakeCurlCommand(command, ts, p)

    def MakeCurlCommandMulti(self, command: str, ts: Process | None = None) -> Process:  # noqa: N802
        if not self._runs:
            self.AddTestRun("curl")
        return self._runs[-1].MakeCurlCommandMulti(command, ts)

    def GetTcpPort(self, name: str | None = None) -> int:  # noqa: N802
        port = self.runtime.allocate_port()
        if name is not None:
            self.Variables[name] = port
        return port

    def SkipUnless(self, *conditions: TypingAny) -> None:  # noqa: N802
        condition = _condition_all(*conditions)
        if not condition():
            pytest.skip(condition.reason)

    def SkipIf(self, *conditions: TypingAny) -> None:  # noqa: N802
        normalized = [_as_condition(condition) for condition in conditions]
        for condition in normalized:
            if condition():
                pytest.skip(condition.reason)

    def PrepareTestPlugin(self, path: str | Path, ts: Process, plugin_args: str = "") -> None:  # noqa: N802
        source = self.resolve_path(path)
        ts.Setup.Copy(source, ts.Env["PROXY_CONFIG_PLUGIN_PLUGIN_DIR"])
        ts.Disk.plugin_config.AddLine(f"{source.name} {plugin_args}".rstrip())

    def PrepareInstalledPlugin(self, name: str, ts: Process, plugin_args: str = "") -> None:  # noqa: N802
        ts.Disk.plugin_config.AddLine(f"{name} {plugin_args}".rstrip())

    def AddAwaitFileContainsTestRun(
            self, name: str, path: str | Path, expression: str, desired_count: int = 1) -> TestRun:  # noqa: N802
        run = self.AddTestRun(name)
        waiter = run.Processes.Process("await", "sleep 60")
        waiter.Ready = When.FileContains(path, expression, desired_count)
        waiter.StartupTimeout = 30
        run.Processes.Default.StartBefore(waiter)
        return run

    def AddConfigReload(
            self,
            ts: Process,
            expect: str = "success",
            token: str | None = None,
            data: str | None = None,  # noqa: N802
            force: bool = False,
            timeout: str | None = "30s",
            initial_wait: float = 1.0,
            refresh_int: float = 0.5,
            delay_start: float | None = None,
            description: str | None = None,
            **_kwargs: TypingAny) -> TestRun:
        token = token or f"urtest-reload-{len(self._runs)}"
        run = self.AddTestRun(description or f"Reload config [{token}]")
        run.Processes.Default.Env = dict(ts.Env)
        command = f"traffic_ctl config reload -m -t {shlex.quote(token)} -w {initial_wait} -r {refresh_int}"
        if timeout is not None:
            command += f" -T {shlex.quote(timeout)}"
        if force:
            command += " --force"
        if data is not None:
            command += f" --data {shlex.quote(data)}"
        run.Command = command
        run.ReturnCode = Choice((0, 2)) if expect == "any" else {"success": 0, "fail": 2, "timeout": 75}[expect]
        run.StillRunningAfter = ts
        if delay_start is not None:
            run.DelayStart = delay_start
        return run

    def execute(self) -> None:
        """Execute every configured step, then validate and clean up processes."""

        if self._executed:
            raise ScenarioError("A Uranium scenario can only execute once")
        self._executed = True
        failures: list[BaseException] = []
        try:
            self.Setup.apply()
            for run in self._runs:
                try:
                    self._execute_run(run)
                except BaseException as error:
                    failures.append(error)
                    if not (run.ContinueOnFail or self.ContinueOnFail):
                        break
        finally:
            for process in reversed(self._all_processes):
                process.stop()
            for process in self._all_processes:
                if process._started:
                    try:
                        process.validate()
                    except BaseException as error:
                        failures.append(error)
            try:
                self.Disk.validate(self.test_directory)
            except BaseException as error:
                failures.append(error)
            self.close_context()
        if failures:
            messages = [f"{type(error).__name__}: {error}" for error in failures]
            raise ScenarioError("\n\n".join(messages))

    def cleanup(self) -> None:
        for process in reversed(self._all_processes):
            process.stop()
        self.close_context()

    def _execute_run(self, run: TestRun) -> None:
        if run.DelayStart:
            time.sleep(float(run.DelayStart))
        run.Setup.apply()
        for process in _process_values(run.StillRunningBefore):
            if not process.is_running():
                raise ScenarioError(f"{process.Name} was expected to be running before {run.Name}")
        processes = list(run.Processes)
        for process in processes:
            process.start()
        keep = set(_process_values(run.StillRunningAfter))
        for process in processes:
            if process in keep or process.is_running() and (process.Ready is not None or
                                                            None in _return_code_values(process.ReturnCode)):
                continue
            process.wait()
        for process in processes:
            if process not in keep and process.is_running():
                process.stop()
            if process._started and not process.is_running():
                process.validate()
        for process in keep:
            if not process.is_running():
                raise ScenarioError(f"{process.Name} was expected to remain running after {run.Name}")

    def _register_process(self, process: Process) -> None:
        if process not in self._all_processes:
            self._all_processes.append(process)

    def _make_ats_process(
            self,
            registry: ProcessRegistry,
            owner: Entity,
            name: str,
            command: str = "traffic_server",
            select_ports: bool = True,
            enable_tls: bool = False,
            enable_cache: bool = True,
            enable_quic: bool = False,
            enable_uds: bool = True,
            enable_cripts: bool = False,
            block_for_debug: bool = False,
            use_traffic_out: bool = True,
            disable_log_checks: bool = False,
            enable_proxy_protocol: bool = False,
            enable_proxy_protocol_cp_src: bool = False,
            **_kwargs: TypingAny) -> Process:
        process = registry.Process(name)
        root = Path(process.RunDirectory)
        paths = {key: root / key for key in ("bin", "config", "plugin", "log", "runtime", "ssl", "storage", "cache")}
        for path in paths.values():
            path.mkdir(parents=True, exist_ok=True)
        for path in (paths["log"], paths["runtime"], paths["ssl"], paths["storage"], paths["cache"]):
            path.chmod(0o777)
        body_factory = paths["config"] / "body_factory"
        body_factory.mkdir(exist_ok=True)
        _link_directory(Path(self.runtime.layout["BINDIR"]), paths["bin"])
        _link_directory(Path(self.runtime.layout["PLUGINDIR"]), paths["plugin"])
        installed_body_factory = Path(self.runtime.layout["SYSCONFDIR"]) / "body_factory"
        if installed_body_factory.is_dir():
            shutil.copytree(installed_body_factory, body_factory, dirs_exist_ok=True)
        min_config = self.runtime.repository_root / "tests" / "tools" / "uranium" / "min_cfg"
        if min_config.is_dir():
            for source in min_config.iterdir():
                if source.is_file() and source.name != "readme.txt":
                    shutil.copy2(source, paths["config"] / source.name)
        process.Variables.update(
            CONFIGDIR=str(paths["config"]),
            BODY_FACTORY_TEMPLATE_DIR=str(body_factory),
            CACHEDIR=str(paths["cache"]),
            LOGDIR=str(paths["log"]),
            RUNTIMEDIR=str(paths["runtime"]),
            LOCALSTATEDIR=str(paths["runtime"]),
            SSLDir=str(paths["ssl"]),
            STORAGEDIR=str(paths["storage"]),
            BINDIR=str(paths["bin"]),
        )
        process.Env.update(
            TS_ROOT=str(root),
            TS_RUNROOT=str(paths["config"] / "runroot.yaml"),
            PROXY_CONFIG_BIN_PATH=str(paths["bin"]),
            PROXY_CONFIG_CONFIG_DIR=str(paths["config"]),
            PROXY_CONFIG_BODY_FACTORY_TEMPLATE_SETS_DIR=str(body_factory),
            PROXY_CONFIG_CACHE_DIR=str(paths["cache"]),
            PROXY_CONFIG_PLUGIN_PLUGIN_DIR=str(paths["plugin"]),
            PROXY_CONFIG_LOG_LOGFILE_DIR=str(paths["log"]),
            PROXY_CONFIG_LOCAL_STATE_DIR=str(paths["runtime"]),
            PROXY_CONFIG_SSL_DIR=str(paths["ssl"]),
            PROXY_CONFIG_STORAGE_DIR=str(paths["storage"]),
            PATH=str(paths["bin"]) + os.pathsep + os.environ.get("PATH", ""),
        )
        files = {
            "records_config": ("records.yaml", "ats:config:records"),
            "cache_config": ("cache.config", "ats:config"),
            "hosting_config": ("hosting.config", "ats:config"),
            "ip_allow_yaml": ("ip_allow.yaml", "ats:config:yaml"),
            "logging_yaml": ("logging.yaml", "ats:config:yaml"),
            "parent_config": ("parent.config", "ats:config"),
            "plugin_config": ("plugin.config", "ats:config"),
            "remap_config": ("remap.config", "ats:config"),
            "remap_yaml": ("remap.yaml", "ats:config:yaml"),
            "splitdns_config": ("splitdns.config", "ats:config"),
            "ssl_multicert_yaml": ("ssl_multicert.yaml", "ats:config:yaml"),
            "sni_yaml": ("sni.yaml", "ats:config:yaml"),
            "storage_yaml": ("storage.yaml", "ats:config:yaml"),
            "runroot_yaml": ("runroot.yaml", "ats:config:yaml"),
        }
        for identifier, (filename, typename) in files.items():
            process.Disk.File(paths["config"] / filename, id=identifier, typename=typename)
        for identifier, filename in (("traffic_out", "traffic.out"), ("diags_log", "diags.log"), ("error_log", "error.log"),
                                     ("squid_log", "squid.log")):
            process.Disk.File(paths["log"] / filename, id=identifier)
        process.Disk.runroot_yaml.AddLines(
            [
                f"runtimedir: {paths['runtime']}",
                f"cachedir: {paths['cache']}",
                f"localstatedir: {paths['runtime']}",
                f"bindir: {paths['bin']}",
                f"prefix: {root}",
                f"logdir: {paths['log']}",
                f"sysconfdir: {paths['config']}",
            ])
        if select_ports:
            process.Variables.port = self.runtime.allocate_port()
            process.Variables.portv6 = self.runtime.allocate_port()
            if enable_tls or enable_quic:
                process.Variables.ssl_port = self.runtime.allocate_port()
                process.Variables.ssl_portv6 = self.runtime.allocate_port()
        else:
            process.Variables.port = 8080
            process.Variables.portv6 = 8080
            if enable_tls or enable_quic:
                process.Variables.ssl_port = 4443
                process.Variables.ssl_portv6 = 4444
        process.Variables.manager_port = self.runtime.allocate_port()
        process.Variables.admin_port = self.runtime.allocate_port()
        process.Variables.uds_path = str(paths["runtime"] / "uds.socket")
        ports = f"{process.Variables.port} {process.Variables.portv6}:ipv6"
        if enable_tls:
            ports += f" {process.Variables.ssl_port}:ssl {process.Variables.ssl_portv6}:ssl:ipv6"
        if enable_quic:
            ports += f" {process.Variables.ssl_port}:quic {process.Variables.ssl_portv6}:quic:ipv6"
        if enable_uds:
            ports += f" {process.Variables.uds_path}"
        process.Disk.records_config.update(
            {
                "proxy.config.config_update_interval_ms": 20,
                "proxy.config.http.server_ports": ports,
                "proxy.config.http.wait_for_cache": 1 if enable_cache else 0,
            })
        if not enable_cache:
            process.Disk.records_config.update({"proxy.config.http.cache.http": 0})
        if enable_quic:
            process.Disk.records_config.update({"proxy.config.udp.threads": 1})
        if enable_cripts:
            compiler = paths["bin"] / "cripts_compiler.sh"
            shutil.copy2(self.runtime.repository_root / "tools" / "cripts" / "compiler.sh", compiler)
            compiler.chmod(0o755)
            process.Variables.cripts_compiler = str(compiler)
            process.Env["ATS_ROOT"] = self.runtime.layout["PREFIX"]
            process.Disk.records_config.update({"proxy.config.plugin.compiler_path": str(compiler)})
            process.StartupTimeout = 60
        process.Env.update(
            PROXY_CONFIG_PROCESS_MANAGER_MGMT_PORT=str(process.Variables.manager_port),
            PROXY_CONFIG_ADMIN_SYNTHETIC_PORT=str(process.Variables.admin_port),
            PROXY_CONFIG_ADMIN_AUTOCONF_PORT=str(process.Variables.admin_port),
        )
        traffic_out = paths["log"] / "traffic.out"
        arguments = ""
        if use_traffic_out:
            arguments += f" --bind_stdout {shlex.quote(str(traffic_out))} --bind_stderr {shlex.quote(str(traffic_out))}"
        if block_for_debug:
            arguments += " --block"
            process.StartupTimeout = 36000
        process.Command = f"{shlex.quote(str(paths['bin'] / command))}{arguments}"
        process.Ready = When.FileContains(process.Disk.diags_log.AbsPath, "NOTE: Traffic Server is fully initialized")
        process.ReturnCode = 0
        if not disable_log_checks:
            process.Disk.diags_log.Content += Testers.ExcludesExpression("FATAL:", "ATS diagnostics must not contain fatal errors")

        def add_ssl_file(filename: str | Path) -> None:
            shutil.copy2(owner.resolve_path(filename), paths["ssl"] / Path(filename).name)

        process.addSSLfile = add_ssl_file
        process.addDefaultSSLFiles = lambda: [
            add_ssl_file(self.runtime.test_tools / "ssl" / filename) for filename in ("server.pem", "server.key")
        ]
        process.addSSLFileFromDefaultTestFolder = lambda filename: add_ssl_file(self.runtime.test_tools / "ssl" / filename)
        process.addPrivateConnectAllowYaml = lambda methods="CONNECT": None
        process.chownForATSProcess = lambda path: process.Setup.Chown(path)
        return process

    def _make_origin(
            self,
            registry: ProcessRegistry,
            owner: Entity,
            name: str,
            port: int | None = None,
            s_port: int | None = None,
            ip: str = "INADDR_LOOPBACK",
            delay: TypingAny = None,
            ssl: bool = False,
            lookup_key: str = "{PATH}",
            clientcert: str = "",
            clientkey: str = "",
            both: bool = False,
            options: Mapping[str, TypingAny] | None = None,
            **_kwargs: TypingAny) -> Process:
        process = registry.Process(name)
        address = {"INADDR_LOOPBACK": "127.0.0.1", "IN6ADDR_LOOPBACK": "::1"}.get(ip, ip)
        data_dir = Path(process.RunDirectory) / "data"
        data_dir.mkdir(parents=True, exist_ok=True)
        process.Variables.DataDir = str(data_dir)
        process.Variables.lookup_key = lookup_key
        command = f"microserver --data-dir {shlex.quote(str(data_dir))} --ip_address {shlex.quote(address)} --lookupkey {shlex.quote(lookup_key)}"
        if delay:
            command += f" --delay {delay}"
        if not ssl:
            port = port or self.runtime.allocate_port()
            process.Variables.Port = port
            command += f" --port {port}"
        if ssl or both:
            s_port = s_port or self.runtime.allocate_port()
            process.Variables.SSL_Port = s_port
            command += " --both" if both else " --ssl"
            key = clientkey or str(self.runtime.test_tools / "microserver" / "ssl" / "server.pem")
            cert = clientcert or str(self.runtime.test_tools / "microserver" / "ssl" / "server.crt")
            command += f" --key {shlex.quote(key)} --cert {shlex.quote(cert)} --s_port {s_port}"
        for flag, value in (options or {}).items():
            command += f" {flag} {value or ''}"
        process.Command = command
        process.Ready = When.PortOpen(s_port if ssl else port, address)
        process.ReturnCode = Any(None, 0)

        def add_response(filename: str, request_header: Mapping[str, TypingAny], response_header: Mapping[str, TypingAny]) -> None:
            try:
                from trlib import Request, Response, Session, Transaction
                request = Request.fromRequestLine(
                    request_header["headers"], request_header.get("body", ""), request_header.get("options"))
                response = Response.fromRequestLine(
                    response_header["headers"], response_header.get("body", ""), response_header.get("options"))
                transaction = Transaction(request, None, response, None, None, None)
                path = data_dir / filename
                if path.exists():
                    document = json.loads(path.read_text())
                    document["sessions"][0]["transactions"].append(transaction.toJSON())
                else:
                    document = {"sessions": [Session(filename, None, None, [transaction]).toJSON()], "meta": {"version": "1.0"}}
                path.write_text(json.dumps(document))
            except ImportError as error:
                raise ScenarioError("traffic-replay is required for microserver tests") from error

        process.addResponse = add_response
        process.addSessionFromFiles = lambda directory: shutil.copytree(owner.resolve_path(directory), data_dir, dirs_exist_ok=True)
        health_request = {"headers": f"GET /ruok HTTP/1.1\r\nHost: {address}\r\n\r\n", "body": ""}
        health_response = {
            "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
            "body": "imok",
            "options": {
                "skipHooks": None
            }
        }
        add_response("healthcheck.json", health_request, health_response)
        return process

    def _make_dns(
            self,
            registry: ProcessRegistry,
            owner: Entity,
            name: str,
            filename: str = "dns_file.json",
            port: int | bool = False,
            ip: str = "INADDR_LOOPBACK",
            rr: bool = False,
            default: TypingAny = None,
            options: Mapping[str, TypingAny] | None = None,
            **_kwargs: TypingAny) -> Process:
        process = registry.Process(name)
        data_dir = Path(process.RunDirectory) / "data"
        data_dir.mkdir(parents=True, exist_ok=True)
        zone = data_dir / filename
        otherwise = [default] if isinstance(default, str) else default
        zone.write_text(json.dumps({"mappings": [], **({"otherwise": otherwise} if otherwise else {})}))
        port = int(port) if port else self.runtime.allocate_port(socket.SOCK_DGRAM)
        process.Variables.update(Port=port, DataDir=str(data_dir), zone_file=str(zone))
        owner.Variables.zone_file = str(zone)
        command = f"microdns {ip} {port} {shlex.quote(str(zone))}"
        if rr:
            command += " --rr"
        for flag, value in (options or {}).items():
            command += f" {flag} {value or ''}"
        process.Command = command
        process.Ready = ReadyCheck(lambda: _dns_ready(port), f"DNS on port {port}")

        def add_records(records: Mapping[str, Sequence[str]] | None = None, jsonFile: str | None = None) -> None:  # noqa: N803
            document = json.loads(zone.read_text())
            for hostname, addresses in (records or {}).items():
                document["mappings"].append({hostname if hostname.endswith(".") else hostname + ".": list(addresses)})
            if jsonFile:
                extra = json.loads(owner.resolve_path(jsonFile).read_text())
                document["mappings"].extend(extra.get("mappings", []))
            zone.write_text(json.dumps(document))

        process.addRecords = add_records
        return process

    def _make_httpbin(
            self,
            registry: ProcessRegistry,
            owner: Entity,
            name: str,
            ip: str = "127.0.0.1",
            port: int | None = None,
            options: Mapping[str, TypingAny] | None = None,
            **_kwargs: TypingAny) -> Process:
        self.SkipUnless(Condition.HasProgram("go-httpbin", "go-httpbin is required"))
        process = registry.Process(name)
        port = port or self.runtime.allocate_port()
        process.Variables.Port = port
        process.Command = f"go-httpbin -host {ip} -port {port}" + "".join(
            f" {flag} {value}" for flag, value in (options or {}).items())
        process.Ready = When.PortOpen(port, ip)
        process.ReturnCode = Any(None, 0)
        return process

    def _make_verifier_server(
            self,
            registry: ProcessRegistry,
            owner: Entity,
            name: str,
            replay_path: str | Path,
            http_ports: Sequence[int] | None = None,
            https_ports: Sequence[int] | None = None,
            http3_ports: Sequence[int] | None = None,
            ssl_cert: str = "",
            ca_cert: str = "",
            verbose: bool = True,
            other_args: str = "",
            context: Mapping[str, TypingAny] | None = None,
            **_kwargs: TypingAny) -> Process:
        process = registry.Process(name)
        http_ports = list(http_ports) if http_ports is not None else [self.runtime.allocate_port()]
        https_ports = list(https_ports) if https_ports is not None else [self.runtime.allocate_port()]
        process.Variables.http_port = http_ports[0] if http_ports else 0
        process.Variables.https_port = https_ports[0] if https_ports else 0
        process.Variables.http3_port = (http3_ports or [0])[0]
        replay = self._prepare_replay(owner, process, replay_path, context)
        command = [str(self.runtime.verifier_bin / "verifier-server"), "run"]
        if http_ports:
            command.extend(["--listen-http", _addresses(http_ports)])
        if https_ports:
            command.extend(["--listen-https", _addresses(https_ports)])
        if https_ports or http3_ports:
            ssl_cert = ssl_cert or str(self.runtime.test_tools / "proxy-verifier" / "ssl" / "server.pem")
            ca_cert = ca_cert or str(self.runtime.test_tools / "proxy-verifier" / "ssl" / "ca.pem")
            command.extend(["--server-cert", ssl_cert, "--ca-certs", ca_cert])
        command.append(str(replay))
        if verbose:
            command.extend(["--verbose", "diag"])
        command.extend(shlex.split(other_args))
        process.Command = shlex.join(command)
        process.Ready = When.PortOpen(http_ports[0]) if http_ports else When.PortOpen(https_ports[0])
        process.Streams.stdout.Content = Testers.ExcludesExpression("Violation", "Proxy Verifier server violations")
        return process

    def _configure_verifier_client(
            self,
            process: Process,
            owner: Entity,
            name: str,
            replay_path: str | Path,
            http_ports: Sequence[int] | None = None,
            https_ports: Sequence[int] | None = None,
            http3_ports: Sequence[int] | None = None,
            keys: str | None = None,
            ssl_cert: str = "",
            ca_cert: str = "",
            verbose: bool = True,
            other_args: str = "",
            run_parallel: bool = False,
            context: Mapping[str, TypingAny] | None = None,
            poll_timeout: int | None = None,
            **_kwargs: TypingAny) -> Process:
        process.Name = name
        replay = self._prepare_replay(owner, process, replay_path, context)
        command = [str(self.runtime.verifier_bin / "verifier-client"), "run", str(replay)]
        if http_ports:
            command.extend(["--connect-http", _addresses(http_ports)])
        if https_ports:
            command.extend(["--connect-https", _addresses(https_ports)])
        if http3_ports:
            command.extend(["--connect-http3", _addresses(http3_ports)])
        if https_ports or http3_ports:
            ssl_cert = ssl_cert or str(self.runtime.test_tools / "proxy-verifier" / "ssl" / "client.pem")
            ca_cert = ca_cert or str(self.runtime.test_tools / "proxy-verifier" / "ssl" / "ca.pem")
            command.extend(["--client-cert", ssl_cert, "--ca-certs", ca_cert])
        if keys:
            command.extend(["--keys", keys])
        if verbose:
            command.extend(["--verbose", "diag"])
        if poll_timeout is not None:
            command.extend(["--poll-timeout", str(poll_timeout)])
        command.extend(shlex.split(other_args))
        if not run_parallel and "thread-limit" not in other_args:
            command.extend(["--thread-limit", "1"])
        process.Command = shlex.join(command)
        process.Streams.stdout.Content = Testers.ExcludesExpression("Violation|Invalid status", "Proxy Verifier client violations")
        return process

    def _prepare_replay(
            self, owner: Entity, process: Process, replay_path: str | Path, context: Mapping[str, TypingAny] | None) -> Path:
        source = owner.resolve_path(replay_path)
        destination = Path(process.RunDirectory) / source.name
        if context and source.is_file():
            destination.write_text(Template(source.read_text()).substitute(context))
        elif source.is_dir():
            shutil.copytree(source, destination, dirs_exist_ok=True)
        else:
            shutil.copy2(source, destination)
        return destination


def _scenario() -> UraniumTest:
    scenario = _active_scenario.get()
    if scenario is None:
        raise ScenarioError("Uranium helpers must be used inside the urtest pytest fixture")
    return scenario


def _validate(value: TypingAny, text: str, path: Path | None, test_directory: Path) -> None:
    if value is None or value == []:
        return
    if isinstance(value, Validator):
        value.validate(text, path, test_directory)
    elif isinstance(value, str):
        GoldValidator(value).validate(text, path, test_directory)
    elif isinstance(value, list):
        ValidatorGroup(value).validate(text, path, test_directory)
    else:
        raise ScenarioError(f"Unsupported output validator: {value!r}")


def _process_values(value: TypingAny) -> list[Process]:
    if value is None:
        return []
    if isinstance(value, Process):
        return [value]
    if isinstance(value, Choice):
        return [item for item in value.values if isinstance(item, Process)]
    if isinstance(value, (list, tuple, set, ProcessGroup)):
        return [item for item in value if isinstance(item, Process)]
    return []


def _return_code_values(value: TypingAny) -> tuple[TypingAny, ...]:
    return value.values if isinstance(value, Choice) else (value,)


def _return_code_matches(actual: int, expected: TypingAny) -> bool:
    return actual in _return_code_values(expected)


def _identifier(name: str) -> str:
    return name.replace(".", "_").replace("-", "_")


def _deep_update(destination: dict[str, TypingAny], source: Mapping[str, TypingAny]) -> None:
    for key, value in source.items():
        if isinstance(value, Mapping) and isinstance(destination.get(key), dict):
            _deep_update(destination[key], value)
        else:
            destination[key] = copy.deepcopy(value)


def _link_directory(source: Path, destination: Path) -> None:
    destination.mkdir(parents=True, exist_ok=True)
    for entry in source.iterdir():
        target = destination / entry.name
        if not target.exists() and not target.is_symlink():
            target.symlink_to(entry)


def _port_open(address: str, port: int) -> bool:
    family = socket.AF_INET6 if ":" in address else socket.AF_INET
    with socket.socket(family, socket.SOCK_STREAM) as probe:
        probe.settimeout(0.1)
        return probe.connect_ex((address, port)) == 0


def _dns_ready(port: int) -> bool:
    try:
        from dnslib import DNSRecord
        DNSRecord.question("urtest-readiness.invalid").send("127.0.0.1", port, timeout=0.1)
        return True
    except (OSError, socket.timeout):
        return False


def _addresses(ports: Sequence[int]) -> str:
    return ",".join(f"127.0.0.1:{port}" for port in ports)


def _command_output(command: Sequence[str]) -> str:
    try:
        result = subprocess.run(command, capture_output=True, text=True, timeout=10, check=False)
    except (OSError, subprocess.SubprocessError):
        return ""
    return result.stdout + result.stderr


def _version(value: str) -> tuple[int, ...]:
    match = re.search(r"\d+(?:\.\d+)+", value)
    return tuple(int(part) for part in match.group().split(".")) if match else ()
