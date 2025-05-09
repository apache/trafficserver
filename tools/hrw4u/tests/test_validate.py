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
from hrw4u.symbols import SymbolResolver
from hrw4u.types import Symbol, VarType


def test_symbol_resolver_validation_keys():
    resolver = SymbolResolver()
    resolver.inject_symbol("foo", Symbol(VarType.BOOL, 0))
    validated = resolver.validate()

    expected_keys = {
        "symbol_table",
        "variables",
        "condition_map",
        "operator_map",
        "functions",
        "sections",
        "suffix_groups",
    }
    assert set(validated.keys()) == expected_keys, f"Unexpected keys: {validated.keys()}"

    for key, value in validated.items():
        if key == "suffix_groups":
            assert isinstance(value, dict), f"suffix_groups should be dict, got {type(value)}"
            for suffix_name, suffix_list in value.items():
                assert isinstance(suffix_name, str)
                assert isinstance(suffix_list, list)
                assert all(isinstance(item, str) for item in suffix_list)
        elif key == "variables":
            assert isinstance(value, list), "'variables' should be a list"
            for item in value:
                assert isinstance(item, str), f"variables should contain strings, got {type(item)}"
                assert ":" in item, f"Expected format 'name: type', got {item}"
        else:
            assert isinstance(value, list), f"{key} should be list, got {type(value)}"
            assert all(isinstance(item, str) for item in value), f"{key} should contain strings only"


def test_symbol_resolver_custom_symbol_presence():
    resolver = SymbolResolver()
    resolver.inject_symbol("bar", Symbol(VarType.INT8, 1))
    validated = resolver.validate()
    assert "bar" in validated["symbol_table"], "'bar' not found in symbol_table"


def test_symbol_resolver_empty_custom_symbols():
    resolver = SymbolResolver()
    validated = resolver.validate()
    assert "symbol_table" in validated
    assert validated["symbol_table"] == []
