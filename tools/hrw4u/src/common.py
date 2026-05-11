#
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
from typing import Final, NoReturn, Protocol, TextIO, Any, Callable

from antlr4.error.ErrorStrategy import BailErrorStrategy, DefaultErrorStrategy
from antlr4 import InputStream, CommonTokenStream

from hrw4u.errors import Hrw4uSyntaxError, ThrowingErrorListener, ErrorCollector, CollectingErrorListener
from hrw4u.formatters import FORMATTERS, ErrorFormatter
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
        r"""(?P<escaped>\{\{.*?\}\})
            |
            (?<!%)\{\s*(?P<func>[a-zA-Z_][a-zA-Z0-9_-]*)\s*\((?P<args>[^)]*)\)\s*\}
            |
            (?<!%)\{(?P<var>[^{}()]+)\}
        """,
        re.VERBOSE | re.DOTALL,
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


class LexerProtocol(Protocol):  # pragma: no cover
    """Protocol for ANTLR lexers."""

    def removeErrorListeners(self) -> None:
        ...

    def addErrorListener(self, listener: Any) -> None:
        ...


class ParserProtocol(Protocol):  # pragma: no cover
    """Protocol for ANTLR parsers."""

    def removeErrorListeners(self) -> None:
        ...

    def addErrorListener(self, listener: Any) -> None:
        ...

    def program(self) -> Any:
        ...

    errorHandler: BailErrorStrategy | DefaultErrorStrategy


class VisitorProtocol(Protocol):  # pragma: no cover
    """Protocol for ANTLR visitors."""

    def visit(self, tree: Any) -> list[str]:
        ...


def fatal(message: str) -> NoReturn:
    """Print error message and exit with failure code."""
    print(message, file=sys.stderr)
    sys.exit(1)


def _build_formatter(error_format: str) -> ErrorFormatter:
    """Instantiate the configured error formatter, falling back to plain."""
    return FORMATTERS.get(error_format, FORMATTERS["plain"])()


def emit_fatal_message(error_format: str, message: str, filename: str = SystemDefaults.DEFAULT_FILENAME) -> NoReturn:
    """Emit a non-syntax error (I/O, argument) via the chosen formatter and exit.

    Plain mode preserves the legacy bare-string output. Structured formats wrap
    the message as a synthetic diagnostic so downstream consumers always see the
    same schema regardless of where the error originated.
    """
    if error_format == 'plain':
        print(message, file=sys.stderr)
    else:
        err = Hrw4uSyntaxError(filename, 0, 0, message, "")
        collector = ErrorCollector(formatter=_build_formatter(error_format))
        collector.add_error(err)
        print(collector.get_error_summary(), file=sys.stderr)
    sys.exit(1)


def emit_fatal_error(error_format: str, error: Hrw4uSyntaxError) -> NoReturn:
    """Emit a single Hrw4uSyntaxError via the chosen formatter and exit.

    Plain mode keeps the legacy ``str(error)`` output (no ``Found 1 error:``
    prefix) so existing CLI consumers see byte-identical output.
    """
    if error_format == 'plain':
        print(str(error), file=sys.stderr)
    else:
        collector = ErrorCollector(formatter=_build_formatter(error_format))
        collector.add_error(error)
        print(collector.get_error_summary(), file=sys.stderr)
    sys.exit(1)


def create_base_parser(description: str) -> tuple[argparse.ArgumentParser, argparse._MutuallyExclusiveGroup]:
    """Create base argument parser with common options."""
    parser = argparse.ArgumentParser(description=description, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("input_file", help="Optional input file path (default: reads from stdin)", nargs="?", default=None)

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
        collect_errors: bool = True,
        max_errors: int = 5,
        error_format: str = "plain") -> tuple[Any, ParserProtocol, ErrorCollector | None]:
    """Create ANTLR parse tree from input content with optional error collection."""
    input_stream = InputStream(content)
    error_collector = None

    if collect_errors:
        error_collector = ErrorCollector(max_errors=max_errors, formatter=_build_formatter(error_format))
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
            emit_fatal_error(error_format, e)
    except Exception as e:
        if collect_errors:
            if error_collector:
                syntax_error = Hrw4uSyntaxError(filename, 0, 0, f"{error_prefix} error: {e}", "")
                error_collector.add_error(syntax_error)
            return None, parser_obj, error_collector
        else:
            emit_fatal_message(error_format, f"{filename}:0:0 - {error_prefix} error: {e}", filename=filename)


def generate_output(
        tree: Any,
        parser_obj: ParserProtocol,
        visitor_class: type[VisitorProtocol],
        filename: str,
        args: Any,
        error_collector: ErrorCollector | None = None,
        extra_kwargs: dict[str, Any] | None = None) -> None:
    """Generate and print output based on mode with optional error collection."""
    if args.ast:
        if tree is not None:
            print(tree.toStringTree(recog=parser_obj))
        elif error_collector and error_collector.has_errors():
            print("Parse tree not available due to syntax errors.")
    else:
        if tree is not None:
            preserve_comments = not getattr(args, 'no_comments', False)
            kwargs: dict[str, Any] = {
                "filename": filename,
                "debug": args.debug,
                "error_collector": error_collector,
                "preserve_comments": preserve_comments
            }
            if extra_kwargs:
                kwargs.update(extra_kwargs)
            visitor = visitor_class(**kwargs)
            try:
                if getattr(args, 'output', None) == 'hrw4u':
                    result = visitor.flatten(tree)
                else:
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
                    visitor_err = e if isinstance(e, Hrw4uSyntaxError) else Hrw4uSyntaxError(
                        filename, 0, 0, f"Visitor error: {e}", "")
                    emit_fatal_error(getattr(args, 'error_format', 'plain'), visitor_err)

    if error_collector and (error_collector.has_errors() or error_collector.has_warnings()):
        print(error_collector.get_error_summary(), file=sys.stderr)
        if error_collector.has_errors() and not args.ast and tree is None:
            sys.exit(1)


def run_main(
        description: str,
        lexer_class: type[LexerProtocol],
        parser_class: type[ParserProtocol],
        visitor_class: type[VisitorProtocol],
        error_prefix: str,
        output_flag_name: str,
        output_flag_help: str,
        add_args: Callable[[argparse.ArgumentParser, argparse._MutuallyExclusiveGroup], None] | None = None,
        pre_process: Callable[[str, str, Any], str] | None = None,
        visitor_kwargs: Callable[[argparse.Namespace], dict[str, Any]] | None = None) -> None:
    """
    Generic main function for hrw4u and u4wrh scripts with bulk compilation support.

    Args:
        description: Description for argument parser
        lexer_class: ANTLR lexer class to use
        parser_class: ANTLR parser class to use
        visitor_class: Visitor class to use
        error_prefix: Error prefix for error messages
        output_flag_name: Name of output flag (e.g., "hrw", "hrw4u")
        output_flag_help: Help text for output flag
        add_args: Optional callback to add extra arguments to the parser and output group
        pre_process: Optional callback(content, filename, args) -> content run before parsing
        visitor_kwargs: Optional callback(args) -> dict of extra kwargs for the visitor
    """
    parser = argparse.ArgumentParser(
        description=description,
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="For bulk compilation to files, use: input1.txt:output1.txt input2.txt:output2.txt ...")

    parser.add_argument(
        "files", help="Input file(s) to parse. Use input:output for bulk file output (default: stdin to stdout)", nargs="*")

    output_group = parser.add_mutually_exclusive_group()
    output_group.add_argument("--ast", action="store_true", help="Produce the ANTLR parse tree only")
    output_group.add_argument(f"--{output_flag_name}", action="store_true", help=output_flag_help)

    parser.add_argument("--no-comments", action="store_true", help="Skip comment preservation (ignore comments in output)")
    parser.add_argument("--debug", action="store_true", help="Enable debug output")
    parser.add_argument(
        "--stop-on-error", action="store_true", help="Stop processing on first error (default: collect and report multiple errors)")
    parser.add_argument(
        "--max-errors",
        type=int,
        default=5,
        dest="max_errors",
        help="Maximum number of errors to report before stopping (default: 5; ignored with --stop-on-error)")
    parser.add_argument(
        "--error-format",
        choices=sorted(FORMATTERS.keys()),
        default="plain",
        dest="error_format",
        help=(
            "Format used for error and warning output on stderr (default: plain). "
            "'json' emits one compact JSON object per input (NDJSON-friendly in bulk mode); "
            "'markdown' emits a rendered report suitable for PR comments and chat. "
            "Columns are always 0-based."))

    if add_args is not None:
        add_args(parser, output_group)

    args = parser.parse_args()

    if not hasattr(args, output_flag_name):
        setattr(args, output_flag_name, False)

    if not (args.ast or getattr(args, output_flag_name)):
        setattr(args, output_flag_name, True)

    extra_kwargs = visitor_kwargs(args) if visitor_kwargs else None

    if not args.files:
        content, filename = process_input(sys.stdin)
        if pre_process is not None:
            try:
                content = pre_process(content, filename, args)
            except Hrw4uSyntaxError as e:
                emit_fatal_error(args.error_format, e)
        tree, parser_obj, error_collector = create_parse_tree(
            content, filename, lexer_class, parser_class, error_prefix, not args.stop_on_error, args.max_errors, args.error_format)
        generate_output(tree, parser_obj, visitor_class, filename, args, error_collector, extra_kwargs)
        return

    if any(':' in f for f in args.files):
        for pair in args.files:
            if ':' not in pair:
                emit_fatal_message(
                    args.error_format,
                    "Error: Mixed formats not allowed. All files must use 'input:output' format for bulk compilation.")

            input_path, output_path = pair.split(':', 1)

            try:
                with open(input_path, 'r', encoding='utf-8') as input_file:
                    content = input_file.read()
                    filename = input_path
            except FileNotFoundError:
                emit_fatal_message(args.error_format, f"Error: Input file '{input_path}' not found", filename=input_path)
            except Exception as e:
                emit_fatal_message(args.error_format, f"Error reading '{input_path}': {e}", filename=input_path)

            if pre_process is not None:
                try:
                    content = pre_process(content, filename, args)
                except Hrw4uSyntaxError as e:
                    emit_fatal_error(args.error_format, e)
            tree, parser_obj, error_collector = create_parse_tree(
                content, filename, lexer_class, parser_class, error_prefix, not args.stop_on_error, args.max_errors,
                args.error_format)

            try:
                with open(output_path, 'w', encoding='utf-8') as output_file:
                    original_stdout = sys.stdout
                    try:
                        sys.stdout = output_file
                        generate_output(tree, parser_obj, visitor_class, filename, args, error_collector, extra_kwargs)
                    finally:
                        sys.stdout = original_stdout
            except Exception as e:
                emit_fatal_message(args.error_format, f"Error writing to '{output_path}': {e}", filename=output_path)
    else:
        for i, input_path in enumerate(args.files):
            if i > 0:
                print("# ---")

            try:
                with open(input_path, 'r', encoding='utf-8') as input_file:
                    content = input_file.read()
                    filename = input_path
            except FileNotFoundError:
                emit_fatal_message(args.error_format, f"Error: Input file '{input_path}' not found", filename=input_path)
            except Exception as e:
                emit_fatal_message(args.error_format, f"Error reading '{input_path}': {e}", filename=input_path)

            if pre_process is not None:
                try:
                    content = pre_process(content, filename, args)
                except Hrw4uSyntaxError as e:
                    emit_fatal_error(args.error_format, e)
            tree, parser_obj, error_collector = create_parse_tree(
                content, filename, lexer_class, parser_class, error_prefix, not args.stop_on_error, args.max_errors,
                args.error_format)

            generate_output(tree, parser_obj, visitor_class, filename, args, error_collector, extra_kwargs)
