#
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

from __future__ import annotations

from antlr4.error.ErrorListener import ErrorListener
from hrw4u.errors import Hrw4uSyntaxError


class ErrorCollector:

    def __init__(self) -> None:
        self.errors: list[Hrw4uSyntaxError] = []

    def add_error(self, error: Hrw4uSyntaxError) -> None:
        self.errors.append(error)

    def has_errors(self) -> bool:
        return bool(self.errors)

    def get_error_summary(self) -> str:
        if not self.errors:
            return "No errors found."

        count = len(self.errors)
        lines = [f"Found {count} error{'s' if count > 1 else ''}:"]
        lines.extend(str(error) for error in self.errors)
        return "\n".join(lines)


class CollectingErrorListener(ErrorListener):

    def __init__(self, filename: str = "<input>", error_collector: ErrorCollector | None = None) -> None:
        super().__init__()
        self.filename = filename
        self.error_collector = error_collector or ErrorCollector()

    def syntaxError(self, recognizer, _offendingSymbol, line, column, msg, e) -> None:
        code_line = ""

        try:
            if hasattr(recognizer, 'inputStream'):
                # Lexer
                input_stream = recognizer.inputStream
            else:
                # Parser
                input_stream = recognizer.getInputStream().tokenSource.inputStream

            if input_stream is not None:
                code_line = input_stream.strdata.splitlines()[line - 1]
        except Exception:
            # If we can't get the source line, continue without it
            pass

        error = Hrw4uSyntaxError(self.filename, line, column, msg, code_line)
        self.error_collector.add_error(error)
