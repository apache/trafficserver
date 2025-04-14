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

import inspect
import re
from typing import Callable, List, Optional
from hrw4u.errors import SymbolResolutionError
import hrw4u.types as types


class ValidatorChain:

    def __init__(self, funcs: Optional[List[Callable[[List[str]], None]]] = None):
        self._validators: List[Callable[[List[str]], None]] = funcs

    def __call__(self, args: List[str]) -> None:
        for fn in self._validators:
            fn(args)

    def _wrap_argwise(self, func: Callable[[str], None]) -> Callable[[List[str]], None]:

        def wrapped(args: List[str]):
            for arg in args:
                func(arg)

        return wrapped

    def _add(self, func: Callable[[List[str]], None]) -> 'ValidatorChain':
        self._validators.append(func)
        return self

    def arg_at(self, index: int, func: Callable[[str], None]) -> 'ValidatorChain':

        def validator(args: List[str]) -> None:
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
        return self._add(self._wrap_argwise(Validator.quoted_or_simple()))

    def nbit_int(self, nbits: int) -> 'ValidatorChain':
        return self._add(self._wrap_argwise(Validator.nbit_int(nbits)))

    def range(self, min_val: int, max_val: int) -> 'ValidatorChain':
        return self._add(self._wrap_argwise(Validator.range(min_val, max_val)))

    def suffix_group(self, group: types.SuffixGroup) -> 'ValidatorChain':
        return self._add(self._wrap_argwise(group.validate))

    def anything(self) -> 'ValidatorChain':
        return self._add(self._wrap_argwise(Validator.anything()))


class Validator:

    @staticmethod
    def arg_count(count: int) -> 'ValidatorChain':

        def validator(args: List[str]) -> None:
            if len(args) != count:
                raise SymbolResolutionError(str(args), f"Invalid number of arguments (expected {count}, got {len(args)})")

        return ValidatorChain([validator])

    @staticmethod
    def min_args(count: int) -> ValidatorChain:

        def validator(args: List[str]) -> None:
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
        simple_re = re.compile(r'^[a-zA-Z0-9_-]+$')

        def validator(value: str) -> None:
            if (value.startswith('"') and value.endswith('"')) or simple_re.fullmatch(value):
                return
            raise SymbolResolutionError(
                value, "Value must be quoted unless it is a simple token (letters, digits, underscore, dash)")

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
    def validate_var_assignment(var_type: types.VarType, value: str, name: str) -> None:
        if var_type == types.VarType.BOOL:
            try:
                types.SuffixGroup.BOOL_FIELDS.validate(value)
            except ValueError:
                raise SymbolResolutionError(name, f"Invalid value '{value}' for bool variable '{name}'")
        elif var_type == types.VarType.INT8:
            try:
                v = int(value)
                if not (0 <= v <= 255):
                    raise SymbolResolutionError(name, f"Invalid value '{value}' for int8 variable '{name}'")
            except ValueError:
                raise SymbolResolutionError(name, f"Expected integer for int8 variable '{name}'")
        elif var_type == types.VarType.INT16:
            try:
                v = int(value)
                if not (0 <= v <= 65535):
                    raise SymbolResolutionError(name, f"Invalid value '{value}' for int16 variable '{name}'")
            except ValueError:
                raise SymbolResolutionError(name, f"Expected integer for int16 variable '{name}'")

    @staticmethod
    def anything() -> Callable[[str], None]:

        def validator(value: str) -> None:
            if not isinstance(value, str):
                raise SymbolResolutionError(value, "Expected a string value")

        return validator
