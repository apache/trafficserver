#!/usr/bin/env python3
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
"""Check that post-processing preserves arbitrary captured bytes."""

import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[1] / "post_process.py"
spec = importlib.util.spec_from_file_location("post_process", SCRIPT)
post_process = importlib.util.module_from_spec(spec)
spec.loader.exec_module(post_process)

PAYLOADS = (
    b"ASCII",
    "café 中文 😀".encode("utf-8"),
    b"caf\xe9",
    b"\xc0\xaf",  # Overlong encoding.
    b"\xed\xa0\x80",  # Encoded surrogate.
    b"\xf0\x9f",  # Truncated sequence.
    b"\x80\xbf",  # Lone continuation bytes.
    bytes(range(0xF5, 0x100)),
    bytes(range(256)),
)


def make_session(url: str, payload: bytes) -> dict:
    text = payload.decode("utf-8", errors="surrogateescape")
    return {
        "connection-time": 123,
        "transactions": [
            {
                "client-request": {
                    "method": "GET",
                    "url": url,
                    "headers": {"fields": [["X-Probe", text]]},
                },
                "proxy-response": {
                    "status": 200,
                    "content": {"data": text, "size": len(payload)},
                },
            }
        ],
    }


def write_dump(path: Path, session: dict) -> None:
    data = json.dumps({"sessions": [session]}, ensure_ascii=False)
    path.write_bytes(data.encode("utf-8", errors="surrogateescape"))


class PostProcessEncodingTest(unittest.TestCase):
    def test_parse_and_write_preserve_bytes(self) -> None:
        for payload in PAYLOADS:
            for indent in (None, 2):
                with self.subTest(payload=payload, indent=indent), tempfile.TemporaryDirectory() as temp:
                    source = Path(temp) / "input.json"
                    output = Path(temp) / "output.json"
                    expected = make_session("/bytes", payload)
                    write_dump(source, expected)
                    parsed = post_process.parse_json(str(source))
                    self.assertEqual(parsed["sessions"], [expected])
                    post_process.write_sessions(parsed["sessions"], str(output), indent)
                    actual = json.loads(output.read_bytes().decode("utf-8", errors="surrogateescape"))
                    self.assertEqual(actual["sessions"], [expected])
                    transaction = actual["sessions"][0]["transactions"][0]
                    self.assertEqual(transaction["proxy-response"]["content"]["data"].encode("utf-8", "surrogateescape"), payload)

    def test_decode_error_is_wrapped(self) -> None:
        error = UnicodeDecodeError("utf-8", b"\xff", 0, 1, "invalid start byte")
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "input.json"
            source.write_text("{}", encoding="utf-8")
            with patch.object(post_process.json, "load", side_effect=error), self.assertLogs(level="ERROR"):
                with self.assertRaises(post_process.ParseJSONError) as caught:
                    post_process.parse_json(str(source))
            self.assertTrue(str(caught.exception))

    def test_invalid_utf8_between_valid_dumps(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "input"
            source.mkdir()
            output = Path(temp) / "output"
            output.mkdir()
            expected = [make_session("/before", b"before"), make_session("/binary", b"caf\xe9"), make_session("/after", b"after")]
            names = ["before.json", "binary.json", "after.json"]
            for name, session in zip(names, expected):
                write_dump(source / name, session)
            with patch.object(post_process.os, "listdir", return_value=names):
                sessions, transactions, errors = post_process.readAndCombine(str(source), 10, 2, False, str(output))
            self.assertEqual((sessions, transactions, dict(errors)), (3, 3, {}))
            actual = post_process.parse_json(str(output / "input_0.json"))["sessions"]
            self.assertEqual(actual, expected)

    def test_cli_keeps_surrounding_dumps(self) -> None:
        for single_line in (False, True):
            with self.subTest(single_line=single_line), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                source = root / "input" / "127"
                source.mkdir(parents=True)
                output = root / "output"
                expected = [make_session(f"/{index}", payload) for index, payload in enumerate(PAYLOADS)]
                for index, session in enumerate(expected):
                    write_dump(source / f"{index:02d}.json", session)
                # A syntax error must also be reported without terminating the worker.
                (source / "broken.json").write_bytes(b'{"sessions":')
                command = [sys.executable, "-X", "utf8", str(SCRIPT), str(source.parent), str(output), "-j", "1"]
                if single_line:
                    command.append("--no-human-readable")
                result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", timeout=15)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertNotIn("Traceback", result.stderr)
                self.assertNotIn("Exception in thread", result.stderr)
                actual = []
                for path in output.glob("*.json"):
                    actual.extend(json.loads(path.read_bytes().decode("utf-8", "surrogateescape"))["sessions"])
                by_url = {session["transactions"][0]["client-request"]["url"]: session for session in actual}
                self.assertEqual(len(actual), len(expected))
                for session in expected:
                    transaction = session["transactions"][0]
                    actual_transaction = by_url[transaction["client-request"]["url"]]["transactions"][0]
                    self.assertEqual(actual_transaction["client-request"], transaction["client-request"])
                    self.assertEqual(actual_transaction["proxy-response"], transaction["proxy-response"])
                self.assertEqual(
                    (output / "summary.txt").read_text(encoding="ascii").strip(),
                    f"Total {len(expected)} sessions and {len(expected)} transactions.",
                )
                self.assertIn("Total errors:", result.stderr)


if __name__ == "__main__":
    unittest.main()
