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
"""
Base visitor class with shared functionality for HRW visitors.

This module provides common patterns for error handling, state management,
debugging, and visitor infrastructure that are shared between the forward
and inverse HRW visitors.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Final
from hrw4u.debugging import Dbg
from hrw4u.states import SectionType
from hrw4u.common import SystemDefaults
from hrw4u.errors import hrw4u_error


class VisitorMixin:
    """Common visitor functionality - moved from common.py since only used here"""
    INDENT_SPACES: Final = 4

    def format_with_indent(self, text: str, indent_level: int) -> str:
        """Format text with proper indentation"""
        return " " * (indent_level * self.INDENT_SPACES) + text


class ErrorHandler:
    """Centralized error handling for visitors and parsers - moved from common.py since only used here"""

    @staticmethod
    def handle_visitor_error(filename: str, ctx: object, exc: Exception, error_collector=None, return_value: str = "") -> str:
        """Standard error handling for visitor methods."""
        from hrw4u.errors import hrw4u_error

        error = hrw4u_error(filename, ctx, exc)
        if error_collector:
            error_collector.add_error(error)
            return return_value
        else:
            raise error

    @staticmethod
    def handle_symbol_error(filename: str, ctx: object, symbol_name: str, exc: Exception, error_collector=None) -> str | None:
        """Handle symbol resolution errors with context."""
        from hrw4u.errors import hrw4u_error, SymbolResolutionError

        if isinstance(exc, SymbolResolutionError):
            error = hrw4u_error(filename, ctx, exc)
        else:
            error = hrw4u_error(filename, ctx, f"symbol error in '{symbol_name}': {exc}")

        if error_collector:
            error_collector.add_error(error)
            return f"ERROR({symbol_name})"
        else:
            raise error


@dataclass(slots=True)
class VisitorState:
    """Encapsulates visitor state that is commonly tracked across visitor implementations."""
    stmt_indent: int = 0
    cond_indent: int = 0
    in_if_block: bool = False
    section_opened: bool = False
    current_section: SectionType | None = None


class BaseHRWVisitor(VisitorMixin):
    """
    Base class for HRW visitors with shared error handling and state management.

    Provides common patterns for:
    - Error collection and handling
    - Debug logging with consistent formatting
    - State management (indentation, sections, etc.)
    - Standard visitor initialization
    - Common output management patterns
    """

    def __init__(
            self,
            filename: str = SystemDefaults.DEFAULT_FILENAME,
            debug: bool = SystemDefaults.DEFAULT_DEBUG,
            error_collector=None) -> None:
        self.filename = filename
        self.error_collector = error_collector
        self.output: list[str] = []

        # State management
        self._state = VisitorState()

        # Debugging
        self._dbg = Dbg(debug)

        # Subclasses should call this after their initialization
        self._initialize_visitor()

    def _initialize_visitor(self) -> None:
        """Hook for subclass-specific initialization. Override as needed."""
        pass

    # Error handling patterns - centralized and consistent
    def handle_visitor_error(self, ctx: Any, exc: Exception, return_value: str = "") -> str:
        """
        Standard error handling for visitor methods.

        Uses centralized error handling from common.py to ensure consistent
        error reporting and collection across all visitor implementations.
        """
        return ErrorHandler.handle_visitor_error(self.filename, ctx, exc, self.error_collector, return_value)

    def handle_symbol_error(self, ctx: Any, symbol_name: str, exc: Exception) -> str | None:
        """
        Handle symbol resolution errors with context.

        Provides consistent error handling for symbol resolution failures
        across different visitor types.
        """
        return ErrorHandler.handle_symbol_error(self.filename, ctx, symbol_name, exc, self.error_collector)

    def safe_visit_with_error_handling(self, method_name: str, ctx: Any, visit_func, *args, **kwargs):
        """
        Generic wrapper for visitor methods with consistent error handling.

        This pattern appears frequently in both visitor implementations and
        provides a standard way to handle exceptions during tree traversal.
        """
        try:
            self._dbg.enter(method_name)
            return visit_func(*args, **kwargs)
        except Exception as exc:
            return self.handle_visitor_error(ctx, exc)
        finally:
            self._dbg.exit(method_name)

    # State management - common patterns
    @property
    def current_section(self) -> SectionType | None:
        """Get current section being processed."""
        return self._state.current_section

    @current_section.setter
    def current_section(self, section: SectionType | None) -> None:
        """Set current section being processed."""
        self._state.current_section = section

    @property
    def stmt_indent(self) -> int:
        """Get current statement indentation level."""
        return self._state.stmt_indent

    @stmt_indent.setter
    def stmt_indent(self, level: int) -> None:
        """Set current statement indentation level."""
        self._state.stmt_indent = level

    @property
    def cond_indent(self) -> int:
        """Get current condition indentation level."""
        return self._state.cond_indent

    @cond_indent.setter
    def cond_indent(self, level: int) -> None:
        """Set current condition indentation level."""
        self._state.cond_indent = level

    # Underscore-prefixed properties for backward compatibility
    @property
    def _stmt_indent(self) -> int:
        """Backward compatibility property - use stmt_indent instead."""
        return self._state.stmt_indent

    @_stmt_indent.setter
    def _stmt_indent(self, level: int) -> None:
        """Backward compatibility property - use stmt_indent instead."""
        self._state.stmt_indent = level

    @property
    def _cond_indent(self) -> int:
        """Backward compatibility property - use cond_indent instead."""
        return self._state.cond_indent

    @_cond_indent.setter
    def _cond_indent(self, level: int) -> None:
        """Backward compatibility property - use cond_indent instead."""
        self._state.cond_indent = level

    def increment_stmt_indent(self) -> None:
        """Increment statement indentation level."""
        self._state.stmt_indent += 1

    def decrement_stmt_indent(self) -> None:
        """Decrement statement indentation level (with bounds checking)."""
        self._state.stmt_indent = max(0, self._state.stmt_indent - 1)

    def increment_cond_indent(self) -> None:
        """Increment condition indentation level."""
        self._state.cond_indent += 1

    def decrement_cond_indent(self) -> None:
        """Decrement condition indentation level (with bounds checking)."""
        self._state.cond_indent = max(0, self._state.cond_indent - 1)

    # Output management patterns
    def emit_line(self, text: str, indent_level: int | None = None) -> None:
        """
        Emit a line of output with proper indentation.

        Uses the shared indentation formatting from VisitorMixin for consistency.
        """
        if indent_level is None:
            indent_level = self._state.stmt_indent
        formatted_line = self.format_with_indent(text, indent_level)
        self.output.append(formatted_line)

    def emit_statement(self, statement: str) -> None:
        """Emit a statement with current statement indentation."""
        self.emit_line(statement, self._state.stmt_indent)

    def emit_condition(self, condition: str) -> None:
        """Emit a condition with current condition indentation."""
        self.emit_line(condition, self._state.cond_indent)

    # Debug logging patterns - consistent formatting

    def debug_enter(self, method_name: str, *args: Any) -> None:
        """Standard debug entry pattern with argument logging."""
        if args:
            arg_strs = [str(arg) for arg in args]
            self._dbg.enter(f"{method_name}: {', '.join(arg_strs)}")
        else:
            self._dbg.enter(method_name)

    def debug_exit(self, method_name: str, result: Any = None) -> None:
        """Standard debug exit pattern with result logging."""
        if result is not None:
            self._dbg.exit(f"{method_name} => {result}")
        else:
            self._dbg.exit(method_name)

    def debug_log(self, message: str) -> None:
        """Standard debug message logging."""
        self._dbg(message)

    # Additional debugging property for compatibility
    @property
    def is_debug(self) -> bool:
        """Check if debug mode is enabled."""
        return self._dbg.enabled

    # Context managers for common patterns
    def indent_context(self, increment: int = 1):
        """Context manager for temporary indentation changes."""

        class IndentContext:

            def __init__(self, visitor: BaseHRWVisitor, inc: int):
                self.visitor = visitor
                self.increment = inc

            def __enter__(self):
                for _ in range(self.increment):
                    self.visitor.increment_stmt_indent()
                return self

            def __exit__(self, exc_type, exc_val, exc_tb):
                for _ in range(self.increment):
                    self.visitor.decrement_stmt_indent()

        return IndentContext(self, increment)

    def debug_context(self, method_name: str, *args: Any):
        """Context manager for debug entry/exit around operations."""

        class DebugContext:

            def __init__(self, visitor: BaseHRWVisitor, method: str, arguments: tuple[Any, ...]):
                self.visitor = visitor
                self.method = method
                self.args = arguments

            def __enter__(self):
                self.visitor.debug_enter(self.method, *self.args)
                return self

            def __exit__(self, exc_type, exc_val, exc_tb):
                if exc_type is None:
                    self.visitor.debug_exit(self.method)
                else:
                    self.visitor.debug_exit(f"{self.method} (exception: {exc_type.__name__})")

        return DebugContext(self, method_name, args)

    # Final output processing - template method pattern

    def get_final_output(self) -> list[str]:
        """
        Get the final output after processing.

        Template method that subclasses can override to add post-processing.
        """
        self._finalize_output()
        return self.output

    def _finalize_output(self) -> None:
        """
        Finalize output processing. Override in subclasses for specific behavior.

        Default implementation does nothing - subclasses can override for specific behavior.
        """
        pass

    # Utility methods for common visitor operations
    def should_continue_on_error(self) -> bool:
        """Determine if processing should continue when errors occur."""
        return self.error_collector is not None

    def get_error_summary(self) -> str | None:
        """Get error summary if errors were collected."""
        if self.error_collector and self.error_collector.has_errors():
            return self.error_collector.get_error_summary()
        return None

    def reset_state(self) -> None:
        """Reset visitor state for reuse."""
        self.output.clear()
        self._state = VisitorState()

    # Compatibility methods for existing visitor code
    def debug(self, message: str) -> None:
        """Alias for debug_log for backward compatibility."""
        self.debug_log(message)

    def emit(self, text: str) -> None:
        """Emit a line of output."""
        self.output.append(self.format_with_indent(text, self.current_indent))

    def increase_indent(self) -> None:
        """Increase indentation level."""
        self.increment_stmt_indent()

    def decrease_indent(self) -> None:
        """Decrease indentation level."""
        self.decrement_stmt_indent()

    @property
    def current_indent(self) -> int:
        """Get current indentation level."""
        return self.stmt_indent

    def handle_error(self, exc: Exception) -> None:
        """Handle error without context."""
        if self.error_collector:
            self.error_collector.add_error(exc)
        else:
            raise exc
