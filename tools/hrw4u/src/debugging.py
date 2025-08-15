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

import sys


class Dbg:
    """Debug utility for hierarchical debug output with indentation."""

    INDENT_SPACES = 4

    def __init__(self, enabled: bool, indent: int = 0) -> None:
        self.enabled = enabled
        self.indent = indent

    def __call__(self, msg: str, *, levels: bool = False, out: bool = False) -> None:
        if self.enabled:
            if levels:
                msg = f"</{msg}>" if out else f"<{msg}>"
            print(f"[debug] {' ' * (self.indent * self.INDENT_SPACES)}{msg}", file=sys.stderr)

    def __enter__(self) -> "Dbg":
        if self.enabled:
            self.indent += 1
        return self

    def __exit__(self, exc_type: type[BaseException] | None, exc_val: BaseException | None, exc_tb: object) -> None:
        if self.enabled:
            self.indent = max(0, self.indent - 1)

    def enter(self, msg: str) -> None:
        if self.enabled:
            self(msg, levels=True)
            self.indent += 1

    def exit(self, msg: str | None = None) -> None:
        if self.enabled:
            self.indent = max(0, self.indent - 1)
            if msg:
                self(msg, levels=True, out=True)
