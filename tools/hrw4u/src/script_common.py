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
from antlr4.error.ErrorStrategy import BailErrorStrategy, DefaultErrorStrategy

from hrw4u.errors import Hrw4uSyntaxError, ThrowingErrorListener
from hrw4u.collector import ErrorCollector, CollectingErrorListener

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


def create_base_parser(description: str) -> tuple[argparse.ArgumentParser, argparse._MutuallyExclusiveGroup]:
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
    parser.add_argument(
        "--collect-errors", action="store_true", help="Collect and report multiple errors instead of stopping at first error")

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
        content: str,
        filename: str,
        lexer_class: type[LexerProtocol],
        parser_class: type[ParserProtocol],
        error_prefix: str,
        collect_errors: bool = False) -> tuple[Any, ParserProtocol, ErrorCollector | None]:
    """Create ANTLR parse tree from input content with optional error collection."""
    input_stream = InputStream(content)
    error_collector = None

    if collect_errors:
        error_collector = ErrorCollector()
        error_listener = CollectingErrorListener(filename=filename, error_collector=error_collector)
    else:
        error_listener = ThrowingErrorListener(filename=filename)

    lexer = lexer_class(input_stream)
    lexer.removeErrorListeners()
    lexer.addErrorListener(error_listener)

    token_stream = CommonTokenStream(lexer)
    parser_obj = parser_class(token_stream)
    parser_obj.removeErrorListeners()
    parser_obj.addErrorListener(error_listener)

    if collect_errors:
        parser_obj.errorHandler = DefaultErrorStrategy()
    else:
        parser_obj.errorHandler = BailErrorStrategy()

    try:
        tree = parser_obj.program()
        return tree, parser_obj, error_collector
    except Hrw4uSyntaxError as e:
        if collect_errors:
            if error_collector:
                error_collector.add_error(e)
            return None, parser_obj, error_collector
        else:
            fatal(str(e))
    except Exception as e:
        if collect_errors:
            if error_collector:
                syntax_error = Hrw4uSyntaxError(filename, 0, 0, f"{error_prefix} error: {e}", "")
                error_collector.add_error(syntax_error)
            return None, parser_obj, error_collector
        else:
            fatal(f"{filename}:0:0 - {error_prefix} error: {e}")


def generate_output(
        tree: Any,
        parser_obj: ParserProtocol,
        visitor_class: type[VisitorProtocol],
        filename: str,
        debug: bool,
        ast_mode: bool,
        error_collector: ErrorCollector | None = None) -> None:
    """Generate and print output based on mode with optional error collection."""
    if ast_mode:
        if tree is not None:
            print(tree.toStringTree(recog=parser_obj))
        elif error_collector and error_collector.has_errors():
            print("Parse tree not available due to syntax errors.")
    else:
        if tree is not None:
            visitor = visitor_class(filename=filename, debug=debug, error_collector=error_collector)
            try:
                result = visitor.visit(tree)
                if result:
                    print("\n".join(result))
            except Exception as e:
                if error_collector:
                    syntax_error = Hrw4uSyntaxError(filename, 0, 0, f"Visitor error: {e}", "")
                    error_collector.add_error(syntax_error)
                else:
                    fatal(str(e))

    if error_collector and error_collector.has_errors():
        print(error_collector.get_error_summary(), file=sys.stderr)
        if not ast_mode and tree is None:
            sys.exit(1)
