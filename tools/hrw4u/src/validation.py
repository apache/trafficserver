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
validation.py

Encapsulates shared validation utilities for use in HRW4U.
"""

from __future__ import annotations

import re
from typing import Callable
from hrw4u.errors import SymbolResolutionError
from hrw4u import states
import hrw4u.types as types
from hrw4u.common import RegexPatterns


class ValidatorChain:

    def __init__(self, funcs: list[Callable[[list[str]], None]] | None = None):
        self._validators: list[Callable[[list[str]], None]] = funcs or []

    def __call__(self, args: list[str]) -> None:
        for fn in self._validators:
            fn(args)

    def _wrap_args(self, func: Callable[[str], None]) -> Callable[[list[str]], None]:

        def wrapped(args: list[str]):
            for arg in args:
                func(arg)

        return wrapped

    def _add(self, func: Callable[[list[str]], None]) -> 'ValidatorChain':
        self._validators.append(func)
        return self


#
# Validators, to be chained
#

    def arg_at(self, index: int, func: Callable[[str], None]) -> 'ValidatorChain':
        """
        Validate a specific argument at the given index.
        """

        def validator(args: list[str]) -> None:
            try:
                func(args[index])
            except IndexError:
                raise SymbolResolutionError(str(args), f"Missing argument at index {index}")

        return self._add(validator)

    def arg_count(self, counts: int) -> 'ValidatorChain':
        return self._add(Validator.arg_count(counts))

    def min_args(self, count: int) -> 'ValidatorChain':
        return self._add(Validator.min_args(count))

    def quoted_or_simple(self) -> 'ValidatorChain':
        return self._add(self._wrap_args(Validator.quoted_or_simple()))

    def http_token(self) -> 'ValidatorChain':
        return self._add(self._wrap_args(Validator.http_token()))

    def nbit_int(self, nbits: int) -> 'ValidatorChain':
        return self._add(self._wrap_args(Validator.nbit_int(nbits)))

    def range(self, min_val: int, max_val: int) -> 'ValidatorChain':
        return self._add(self._wrap_args(Validator.range(min_val, max_val)))

    def suffix_group(self, group: types.SuffixGroup) -> 'ValidatorChain':
        return self._add(self._wrap_args(group.validate))


class Validator:
    # Use shared compiled regex patterns for performance
    _SIMPLE_TOKEN_RE = RegexPatterns.SIMPLE_TOKEN
    _HTTP_TOKEN_RE = RegexPatterns.HTTP_TOKEN
    _REGEX_LITERAL_RE = RegexPatterns.REGEX_LITERAL
    _PERCENT_RE = RegexPatterns.PERCENT_BLOCK
    _PERCENT_INLINE_RE = RegexPatterns.PERCENT_INLINE
    _PERCENT_PATTERN = RegexPatterns.PERCENT_PATTERN

    @staticmethod
    def arg_count(count: int) -> 'ValidatorChain':

        def validator(args: list[str]) -> None:
            if len(args) != count:
                raise SymbolResolutionError(str(args), f"Invalid number of arguments (expected {count}, got {len(args)})")

        return ValidatorChain([validator])

    @staticmethod
    def min_args(count: int) -> ValidatorChain:

        def validator(args: list[str]) -> None:
            if len(args) < count:
                raise SymbolResolutionError(str(args), f"At least {count} argument(s) required (got {len(args)})")

        return ValidatorChain([validator])

    @staticmethod
    def nbit_int(nbits: int) -> Callable[[str], None]:
        max_val = (1 << nbits) - 1

        def validator(value: str) -> None:
            try:
                val = int(value)
                if not (0 <= val <= max_val):
                    raise SymbolResolutionError(value, f"Value must be a {nbits}-bit unsigned integer (0–{max_val})")
            except ValueError:
                raise SymbolResolutionError(value, "Expected an integer value")

        return validator

    @staticmethod
    def range(min_val: int, max_val: int) -> Callable[[str], None]:

        def validator(value: str) -> None:
            try:
                val = int(value)
                if not (min_val <= val <= max_val):
                    raise SymbolResolutionError(value, f"Value must be in range {min_val}–{max_val}")
            except ValueError:
                raise SymbolResolutionError(value, "Expected an integer value")

        return validator

    @staticmethod
    def quoted_or_simple() -> Callable[[str], None]:

        def validator(value: str) -> None:
            if (value.startswith('"') and value.endswith('"')) or Validator._SIMPLE_TOKEN_RE.fullmatch(value):
                return
            raise SymbolResolutionError(
                value, "Value must be quoted unless it is a simple token (letters, digits, underscore, dash)")

        return validator

    @staticmethod
    def http_token() -> Callable[[str], None]:

        def validator(value: str) -> None:
            if Validator._HTTP_TOKEN_RE.fullmatch(value):
                return
            raise SymbolResolutionError(value, "HTTP token/header not valid, illegal characters or format.")

        return validator

    @staticmethod
    def suffix_group(group: types.SuffixGroup) -> Callable[[str], None]:

        def validator(value: str) -> None:
            try:
                group.validate(value)
            except ValueError as e:
                raise SymbolResolutionError(value, str(e))

        return validator

    @staticmethod
    def validate_assignment(var_type: types.VarType, value: str, name: str) -> None:
        match var_type:
            case types.VarType.BOOL:
                try:
                    types.SuffixGroup.BOOL_FIELDS.validate(value)
                except ValueError:
                    raise SymbolResolutionError(name, f"Invalid value '{value}' for bool variable '{name}'")

            case types.VarType.INT8:
                try:
                    v = int(value)
                    if not (0 <= v <= 255):
                        raise SymbolResolutionError(name, f"Invalid value '{value}' for int8 variable '{name}'")
                except ValueError:
                    raise SymbolResolutionError(name, f"Expected integer for int8 variable '{name}'")

            case types.VarType.INT16:
                try:
                    v = int(value)
                    if not (0 <= v <= 65535):
                        raise SymbolResolutionError(name, f"Invalid value '{value}' for int16 variable '{name}'")
                except ValueError:
                    raise SymbolResolutionError(name, f"Expected integer for int16 variable '{name}'")

    @staticmethod
    def needs_quotes(value: str) -> bool:
        if not value:
            return True
        if value.startswith('"') and value.endswith('"'):
            return False
        if Validator._SIMPLE_TOKEN_RE.fullmatch(value):
            return False
        if Validator._REGEX_LITERAL_RE.match(value):
            return False
        try:
            int(value)
            return False
        except ValueError:
            return True

    @staticmethod
    def quote_if_needed(value: str) -> str:
        return f'"{value}"' if Validator.needs_quotes(value) else value

    @staticmethod
    def logic_modifier() -> Callable[[str], None]:

        def validator(value: str) -> None:
            if value.upper() not in states.ALL_MODIFIERS:
                raise SymbolResolutionError(value, f"Invalid logic modifier: {value}")

        return validator

    @staticmethod
    def percent_block() -> Callable[[str], None]:

        def validator(value: str) -> None:
            if not value.startswith("%{") or not value.endswith("}"):
                raise SymbolResolutionError(value, "Invalid percent block format (must be %{...})")

        return validator

    @staticmethod
    def set_format() -> Callable[[str], None]:

        def validator(value: str) -> None:
            if not ((value.startswith('[') and value.endswith(']')) or (value.startswith('(') and value.endswith(')'))):
                raise SymbolResolutionError(value, "Set must be enclosed in [] or ()")

        return validator

    @staticmethod
    def iprange_format() -> Callable[[str], None]:

        def validator(value: str) -> None:
            if not (value.startswith('{') and value.endswith('}')):
                raise SymbolResolutionError(value, "IP range must be enclosed in {}")

        return validator
