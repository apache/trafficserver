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

from functools import cache
from rapidfuzz import fuzz, process
from hrw4u.states import SectionType
import hrw4u.tables as tables


class SuggestionEngine:

    def __init__(self, similarity_threshold: float = 70.0, max_suggestions: int = 2):
        self.similarity_threshold = similarity_threshold
        self.max_suggestions = max_suggestions

    def _build_symbol_index_from_table(self, table_map: dict) -> set[str]:
        symbols = set()

        try:
            from hrw4u.lsp.documentation import LSP_NAMESPACE_DOCUMENTATION, LSP_SUB_NAMESPACE_DOCUMENTATION
            symbols.update(LSP_NAMESPACE_DOCUMENTATION.keys())
            symbols.update(LSP_SUB_NAMESPACE_DOCUMENTATION.keys())
        except ImportError:
            pass

        for key in table_map.keys():
            symbols.add(key[:-1] if key.endswith('.') else key)

        return symbols

    @property
    def _assignment_symbols(self) -> set[str]:
        if not hasattr(self, '_cached_assignment_symbols'):
            self._cached_assignment_symbols = self._build_symbol_index_from_table(tables.OPERATOR_MAP)
        return self._cached_assignment_symbols

    @property
    def _condition_symbols(self) -> set[str]:
        if not hasattr(self, '_cached_condition_symbols'):
            self._cached_condition_symbols = self._build_symbol_index_from_table(tables.CONDITION_MAP)
        return self._cached_condition_symbols

    @property
    def _function_symbols(self) -> set[str]:
        if not hasattr(self, '_cached_function_symbols'):
            self._cached_function_symbols = set(tables.FUNCTION_MAP.keys())
        return self._cached_function_symbols

    @property
    def _statement_function_symbols(self) -> set[str]:
        if not hasattr(self, '_cached_statement_function_symbols'):
            self._cached_statement_function_symbols = set(tables.STATEMENT_FUNCTION_MAP.keys())
        return self._cached_statement_function_symbols

    def _get_contextual_symbols(self, context_type: str, section: SectionType | None = None) -> set[str]:
        symbols = {
            'assignment': self._assignment_symbols,
            'condition': self._condition_symbols,
            'function': self._function_symbols,
            'statement_function': self._statement_function_symbols
        }.get(
            context_type,
            self._assignment_symbols | self._condition_symbols | self._function_symbols | self._statement_function_symbols)

        if section and context_type in ['assignment', 'condition']:
            return {s for s in symbols if self._is_symbol_valid_in_section(s, section, context_type)}

        return symbols

    def _is_symbol_valid_in_section(self, symbol: str, section: SectionType, context_type: str) -> bool:
        table_map = tables.OPERATOR_MAP if context_type == 'assignment' else tables.CONDITION_MAP
        tuple_index = 3 if context_type == 'assignment' else 3

        for key, data in table_map.items():
            if (key == symbol or key == f"{symbol}.") and data[tuple_index]:
                return section in data[tuple_index]

        return True

    def get_suggestions(
            self,
            input_symbol: str,
            context_type: str,
            section: SectionType | None = None,
            declared_vars: list[str] | None = None) -> list[str]:
        return [match[0] for match in self._get_suggestions_with_scores(input_symbol, context_type, section, declared_vars)]

    def get_detailed_suggestions(
            self,
            input_symbol: str,
            context_type: str,
            section: SectionType | None = None,
            declared_vars: list[str] | None = None) -> list[tuple[str, float]]:
        return self._get_suggestions_with_scores(input_symbol, context_type, section, declared_vars)

    def _get_suggestions_with_scores(
            self,
            input_symbol: str,
            context_type: str,
            section: SectionType | None = None,
            declared_vars: list[str] | None = None) -> list[tuple[str, float]]:
        candidates = list(self._get_contextual_symbols(context_type, section))

        if declared_vars and context_type == 'condition':
            candidates.extend(declared_vars)
        if not candidates:
            return []

        matches = process.extract(
            input_symbol.lower(), [c.lower() for c in candidates],
            scorer=fuzz.ratio,
            limit=self.max_suggestions,
            score_cutoff=self.similarity_threshold)

        candidate_map = {c.lower(): c for c in candidates}
        results = [(candidate_map[match[0]], match[1]) for match in matches]

        if not results and '.' in input_symbol:
            prefix_results = self._get_prefix_based_suggestions(input_symbol, candidates)
            results = [(match, self.similarity_threshold) for match in prefix_results]

        return results

    def _get_prefix_based_suggestions(self, input_symbol: str, candidates: list[str]) -> list[str]:
        input_parts = input_symbol.split('.')
        best_matches = []

        for candidate in candidates:
            candidate_base = candidate.rstrip('.')
            candidate_parts = candidate_base.split('.')

            if len(candidate_parts) <= len(input_parts):
                input_prefix = '.'.join(input_parts[:len(candidate_parts)])
                similarity = fuzz.ratio(input_prefix.lower(), candidate_base.lower())

                if similarity >= self.similarity_threshold:
                    best_matches.append((candidate_base, similarity))

        best_matches.sort(key=lambda x: x[1], reverse=True)
        return [match[0] for match in best_matches[:self.max_suggestions]]
