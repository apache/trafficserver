#!/usr/bin/env python3
'''
Licensed to the Apache Software Foundation (ASF) under one
or more contributor license agreements.  See the NOTICE file
distributed with this work for additional information
regarding copyright ownership.  The ASF licenses this file
to you under the Apache License, Version 2.0 (the
"License"); you may not use this file except in compliance
with the License.  You may obtain a copy of the License at

 http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing,
software distributed under the License is distributed on an
"AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
KIND, either express or implied.  See the License for the
specific language governing permissions and limitations
under the License.
'''
"""Verify incomplete sessions do not prevent valid sessions from being written."""
import importlib.util
import json
import logging
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "post_process.py"
spec = importlib.util.spec_from_file_location("post_process", SCRIPT)
post_process = importlib.util.module_from_spec(spec)
spec.loader.exec_module(post_process)

INVALID_SESSIONS = {
    "null": None,
    "empty": {},
    "list": [1],
    "string": "abc",
    "missing_timestamp": {
        "transactions": []
    },
    "legacy_timestamp": {
        "start-time": 123,
        "transactions": []
    },
    "zero_timestamp": {
        "connection-time": 0,
        "transactions": []
    },
    "zero_timestamp_with_fallback": {
        "connection-time": 0,
        "start-time": 123,
        "transactions": []
    },
    "connection_timestamp": {
        "connection-time": 123,
        "transactions": []
    },
    "string_timestamp": {
        "connection-time": "not-a-number",
        "transactions": []
    },
}


def valid_session(url: str) -> dict:
    """Create a complete transaction served locally by the proxy."""
    return {
        "connection-time": 123,
        "transactions":
            [
                {
                    "client-request": {
                        "method": "GET",
                        "url": url,
                        "headers": {
                            "fields": [["Host", "example.test"]]
                        }
                    },
                    "proxy-response": {
                        "status": 200
                    },
                }
            ],
    }


def write_input(root: Path, sessions: list) -> tuple[Path, Path]:
    """Write one dump file in the client directory expected by the CLI."""
    input_dir = root / "input" / "127"
    input_dir.mkdir(parents=True)
    output_dir = root / "output"
    output_dir.mkdir()
    (input_dir / "sessions.json").write_text(json.dumps({"sessions": sessions}), encoding="utf-8")
    return input_dir, output_dir


class PostProcessTest(unittest.TestCase):

    def assert_valid_sessions(self, output_dir: Path) -> None:
        files = list(output_dir.glob("*.json"))
        self.assertEqual(len(files), 1)
        sessions = json.loads(files[0].read_text(encoding="utf-8"))["sessions"]
        self.assertEqual([session["transactions"][0]["client-request"]["url"] for session in sessions], ["/before", "/after"])

    def test_complete_sessions(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            input_dir, output_dir = write_input(Path(temp), [valid_session("/before"), valid_session("/after")])
            sessions, transactions, _ = post_process.readAndCombine(str(input_dir), 10, 2, True, str(output_dir))
            self.assertEqual((sessions, transactions), (2, 2))
            self.assert_valid_sessions(output_dir)

    def test_incomplete_sessions(self) -> None:
        expected_timestamps = {
            "legacy_timestamp": "start-time: 123",
            "zero_timestamp_with_fallback": "start-time: 123",
            "connection_timestamp": "connection-time: 123",
            "string_timestamp": "connection-time: not-a-number",
        }
        for name, invalid in INVALID_SESSIONS.items():
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temp:
                input_dir, output_dir = write_input(Path(temp), [valid_session("/before"), invalid, valid_session("/after")])
                with self.assertLogs(level=logging.DEBUG) as logs:
                    sessions, transactions, _ = post_process.readAndCombine(str(input_dir), 10, 2, True, str(output_dir))
                self.assertEqual((sessions, transactions), (2, 2))
                self.assert_valid_sessions(output_dir)
                if name in expected_timestamps:
                    self.assertIn(expected_timestamps[name], "\n".join(logs.output))

    def test_cli_preserves_valid_sessions(self) -> None:
        for name, invalid in INVALID_SESSIONS.items():
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temp:
                input_dir, output_dir = write_input(Path(temp), [valid_session("/before"), invalid, valid_session("/after")])
                result = subprocess.run(
                    [sys.executable, str(SCRIPT),
                     str(input_dir.parent),
                     str(output_dir), "-j", "1", "--debug"],
                    capture_output=True,
                    text=True,
                    timeout=15)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertNotIn("Exception in thread", result.stderr)
                self.assertNotIn("Traceback", result.stderr)
                self.assert_valid_sessions(output_dir)
                self.assertEqual((output_dir / "summary.txt").read_text().strip(), "Total 2 sessions and 2 transactions.")


if __name__ == "__main__":
    unittest.main()
