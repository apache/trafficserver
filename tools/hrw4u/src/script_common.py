#!/usr/bin/env python3
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
"""Shared utilities for hrw4u and u4wrh scripts."""

from __future__ import annotations

import argparse
import sys
from typing import Final, NoReturn, Protocol, TextIO, Any

from antlr4 import CommonTokenStream, InputStream
from antlr4.error.ErrorStrategy import BailErrorStrategy

from hrw4u.errors import Hrw4uSyntaxError, ThrowingErrorListener

# Modern Python 3.11+ requirement
REQUIRED_PYTHON: Final[tuple[int, int]] = (3, 11)

if sys.version_info < REQUIRED_PYTHON:
    raise RuntimeError(f"This program requires Python {REQUIRED_PYTHON[0]}.{REQUIRED_PYTHON[1]} or later.")


class LexerProtocol(Protocol):
    """Protocol for ANTLR lexers."""

    def removeErrorListeners(self) -> None:
        ...

    def addErrorListener(self, listener: Any) -> None:
        ...


class ParserProtocol(Protocol):
    """Protocol for ANTLR parsers."""

    def removeErrorListeners(self) -> None:
        ...

    def addErrorListener(self, listener: Any) -> None:
        ...

    def program(self) -> Any:
        ...

    errorHandler: Any


class VisitorProtocol(Protocol):
    """Protocol for ANTLR visitors."""

    def visit(self, tree: Any) -> list[str]:
        ...


def fatal(message: str) -> NoReturn:
    """Print error message and exit with failure code."""
    print(message, file=sys.stderr)
    sys.exit(1)


def create_base_parser(description: str) -> argparse.ArgumentParser:
    """Create base argument parser with common options."""
    parser = argparse.ArgumentParser(description=description, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument(
        "input_file",
        help="The input file to parse (default: stdin)",
        nargs="?",
        type=argparse.FileType("r", encoding="utf-8"),
        default=sys.stdin)

    output_group = parser.add_mutually_exclusive_group()
    output_group.add_argument("--ast", action="store_true", help="Produce the ANTLR parse tree only")

    parser.add_argument("--debug", action="store_true", help="Enable debug output")

    return parser, output_group


def process_input(input_file: TextIO) -> tuple[str, str]:
    """Read input content and determine filename."""
    content = input_file.read()

    if input_file is not sys.stdin:
        filename = input_file.name
        input_file.close()
    else:
        filename = "<stdin>"

    return content, filename


def create_parse_tree(
        content: str, filename: str, lexer_class: type[LexerProtocol], parser_class: type[ParserProtocol],
        error_prefix: str) -> tuple[Any, ParserProtocol]:
    """Create ANTLR parse tree from input content."""
    input_stream = InputStream(content)

    # Configure lexer
    lexer = lexer_class(input_stream)
    lexer.removeErrorListeners()
    lexer.addErrorListener(ThrowingErrorListener(filename=filename))

    # Configure parser
    token_stream = CommonTokenStream(lexer)
    parser_obj = parser_class(token_stream)
    parser_obj.removeErrorListeners()
    parser_obj.addErrorListener(ThrowingErrorListener(filename=filename))
    parser_obj.errorHandler = BailErrorStrategy()

    try:
        return parser_obj.program(), parser_obj
    except Hrw4uSyntaxError as e:
        fatal(str(e))
    except Exception as e:
        fatal(f"{filename}:0:0 - {error_prefix} error: {e}")


def generate_output(
        tree: Any, parser_obj: ParserProtocol, visitor_class: type[VisitorProtocol], filename: str, debug: bool,
        ast_mode: bool) -> None:
    """Generate and print output based on mode."""
    if ast_mode:
        print(tree.toStringTree(recog=parser_obj))
    else:
        visitor = visitor_class(filename=filename, debug=debug)
        try:
            result = visitor.visit(tree)
            print("\n".join(result))
        except Exception as e:
            fatal(str(e))
