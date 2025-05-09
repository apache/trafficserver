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
from antlr4.error.ErrorListener import ErrorListener
from antlr4.error.Errors import ParseCancellationException


class Hrw4uSyntaxError(Exception):

    def __init__(self, filename, line, column, message, source_line=""):
        super().__init__(format_error(filename, line, column, message, source_line))


class ThrowingErrorListener(ErrorListener):

    def __init__(self, filename="<input>"):
        super().__init__()
        self.filename = filename

    def syntaxError(self, recognizer, offendingSymbol, line, column, msg, e):
        raise Hrw4uSyntaxError(self.filename, line, column, msg)


class SymbolResolutionError(Exception):

    def __init__(self, name, message=None):
        self.name = name
        super().__init__(message or f"Unrecognized symbol: '{name}'")


def wrap_error(filename, ctx, exc):
    if isinstance(exc, Hrw4uSyntaxError):
        return exc

    line = ctx.start.line
    col = ctx.start.column
    message = str(exc)

    try:
        input_stream = ctx.start.getInputStream()
        token_start = ctx.start.start
        raw_line = input_stream.getText(token_start - col, token_start + 250)
        source_line = raw_line.splitlines()[0]
    except Exception:
        source_line = ""

    return Hrw4uSyntaxError(filename, line, col, message, source_line)


def format_error(filename, line, col, message, source_line=""):
    header = f"{filename}:{line}:{col}: error: {message}"

    if source_line:
        lineno_str = f"{line:4d}"
        bar = " |"
        code_line = f"{lineno_str}{bar} {source_line}"
        pointer_line = f"{' ' * 4}{bar} {' ' * col}^"
        return f"{header}\n{code_line}\n{pointer_line}"

    try:
        with open(filename, "r") as f:
            lines = f.readlines()
            if 1 <= line <= len(lines):
                src_line = lines[line - 1].rstrip("\n")
                lineno_str = f"{line:4d}"
                bar = " |"
                code_line = f"{lineno_str}{bar} {src_line}"
                pointer_line = f"{' ' * 4}{bar} {' ' * col}^"
                return f"{header}\n{code_line}\n{pointer_line}"
    except Exception:
        pass

    return header
