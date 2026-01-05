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

import argparse
import re
import sys
from typing import Final, NoReturn, Protocol, TextIO, Any

from antlr4.error.ErrorStrategy import BailErrorStrategy, DefaultErrorStrategy
from antlr4 import InputStream, CommonTokenStream

from hrw4u.errors import Hrw4uSyntaxError, ThrowingErrorListener, ErrorCollector, CollectingErrorListener
from hrw4u.types import MagicStrings


class RegexPatterns:
    """Compiled regex patterns for reuse across modules"""
    SIMPLE_TOKEN: Final = re.compile(r'^[@a-zA-Z0-9_-]+$')
    HTTP_TOKEN: Final = re.compile(r'^[!#$%&\'*+.^_`|~0-9A-Za-z-]+$')
    HTTP_HEADER_NAME: Final = re.compile(r'^(?:@[!#$%&\'*+^_`|~0-9A-Za-z-]+|[!#$%&\'*+^_`|~0-9A-Za-z-]+)$')
    REGEX_LITERAL: Final = re.compile(r'^/(?:\\.|[^/\r\n])+/$')
    PERCENT_BLOCK: Final = re.compile(r"^\%\{([A-Z0-9_-]+)(?::(.*))?\}$")
    PERCENT_INLINE: Final = re.compile(r"%\{([A-Z0-9_-]+)(?::(.*?))?\}")
    PERCENT_PATTERN: Final = re.compile(r'%\{([^}]+)\}')
    SUBSTITUTE_PATTERN: Final = re.compile(
        r"""(?<!%)\{\s*(?P<func>[a-zA-Z_][a-zA-Z0-9_-]*)\s*\((?P<args>[^)]*)\)\s*\}
            |
            (?<!%)\{(?P<var>[^{}()]+)\}
        """,
        re.VERBOSE,
    )

    # Additional performance patterns
    IDENTIFIER: Final = re.compile(r'^[a-zA-Z_][a-zA-Z0-9_]*$')
    WHITESPACE: Final = re.compile(r'\s+')
    COMMENT_BLOCK: Final = re.compile(r'/\*.*?\*/', re.DOTALL)
    STRING_INTERPOLATION: Final = re.compile(r'\{([a-zA-Z_][a-zA-Z0-9_.-]*(?:\([^)]*\))?)\}', re.MULTILINE)


class SystemDefaults:
    """System-wide default constants"""
    DEFAULT_FILENAME: Final = "<stdin>"
    DEFAULT_DEBUG: Final = False
    DEFAULT_CONFIGURABLE: Final = False
    LINE_NUMBER_WIDTH: Final = 4
    INDENT_SPACES: Final = 4
    DEBUG_PREFIX: Final = "[debug]"


class HeaderOperations:
    """Operation constants for various resource types"""
    OPERATIONS: Final = (MagicStrings.RM_HEADER.value, MagicStrings.SET_HEADER.value)
    ADD_OPERATION: Final = MagicStrings.ADD_HEADER.value
    COOKIE_OPERATIONS: Final = (MagicStrings.RM_COOKIE.value, MagicStrings.SET_COOKIE.value)
    DESTINATION_OPERATIONS: Final = (MagicStrings.RM_DESTINATION.value, MagicStrings.SET_DESTINATION.value)


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

    errorHandler: BailErrorStrategy | DefaultErrorStrategy


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
        "--stop-on-error", action="store_true", help="Stop processing on first error (default: collect and report multiple errors)")

    return parser, output_group


def process_input(input_file: TextIO) -> tuple[str, str]:
    """Read input content and determine filename."""
    content = input_file.read()

    if input_file is not sys.stdin:
        filename = input_file.name
        input_file.close()
    else:
        filename = SystemDefaults.DEFAULT_FILENAME

    return content, filename


def create_parse_tree(
        content: str,
        filename: str,
        lexer_class: type[LexerProtocol],
        parser_class: type[ParserProtocol],
        error_prefix: str,
        collect_errors: bool = True) -> tuple[Any, ParserProtocol, ErrorCollector | None]:
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
        args: Any,
        error_collector: ErrorCollector | None = None) -> None:
    """Generate and print output based on mode with optional error collection."""
    if args.ast:
        if tree is not None:
            print(tree.toStringTree(recog=parser_obj))
        elif error_collector and error_collector.has_errors():
            print("Parse tree not available due to syntax errors.")
    else:
        if tree is not None:
            preserve_comments = not getattr(args, 'no_comments', False)
            merge_sections = not getattr(args, 'no_merge_sections', False)

            # Build visitor kwargs based on what the visitor class supports
            visitor_kwargs = {
                'filename': filename,
                'debug': args.debug,
                'error_collector': error_collector,
                'preserve_comments': preserve_comments
            }

            # Only add merge_sections if the visitor supports it (u4wrh)
            import inspect
            if 'merge_sections' in inspect.signature(visitor_class.__init__).parameters:
                visitor_kwargs['merge_sections'] = merge_sections

            visitor = visitor_class(**visitor_kwargs)
            try:
                result = visitor.visit(tree)
                if result:
                    print("\n".join(result))
            except Exception as e:
                if error_collector:
                    syntax_error = Hrw4uSyntaxError(filename, 0, 0, f"Visitor error: {e}", "")
                    if hasattr(e, '__notes__') and e.__notes__:
                        for note in e.__notes__:
                            syntax_error.add_note(note)
                    error_collector.add_error(syntax_error)
                else:
                    fatal(str(e))

    if error_collector and error_collector.has_errors():
        print(error_collector.get_error_summary(), file=sys.stderr)
        if not args.ast and tree is None:
            sys.exit(1)
