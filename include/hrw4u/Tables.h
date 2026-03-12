/*
  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#pragma once

#include <string>
#include <string_view>
#include <optional>
#include <unordered_map>

#include "hrw4u/Types.h"
#include "hrw4u/ObjTypes.h"

namespace hrw4u
{

enum class OperatorPrefix { NONE, SET_ADD_RM, SET_RM };

struct MapParams {
  std::string        target;
  SectionSet         sections;
  SuffixGroup        suffix_group          = SuffixGroup::URL_FIELDS;
  OperatorPrefix     op_prefix             = OperatorPrefix::NONE;
  hrw::ConditionType cond_type             = hrw::ConditionType::NONE;
  hrw::OperatorType  op_type               = hrw::OperatorType::NONE;
  bool               upper                 = false;
  bool               prefix                = false;
  bool               has_suffix_validation = false;
  bool               bare                  = false; // Don't wrap generated target in %{}

  [[nodiscard]] bool
  valid_for_section(SectionType section) const
  {
    return sections.empty() || sections.count(section) > 0;
  }
};

struct ResolveResult {
  std::string        target;
  std::string        suffix;
  std::string        error_message;
  OperatorPrefix     op_prefix = OperatorPrefix::NONE;
  hrw::ConditionType cond_type = hrw::ConditionType::NONE;
  hrw::OperatorType  op_type   = hrw::OperatorType::NONE;
  bool               success   = false;
  bool               prefix    = false;

  [[nodiscard]] explicit
  operator bool() const
  {
    return success;
  }
  [[nodiscard]] hrw::OperatorType get_operator_type(bool is_append = false, bool is_remove = false) const;
};

class SymbolResolver
{
public:
  SymbolResolver();

  [[nodiscard]] ResolveResult              resolve_operator(std::string_view symbol, SectionType section) const;
  [[nodiscard]] ResolveResult              resolve_condition(std::string_view symbol, SectionType section) const;
  [[nodiscard]] ResolveResult              resolve_function(std::string_view name, SectionType section) const;
  [[nodiscard]] ResolveResult              resolve_statement_function(std::string_view name, SectionType section) const;
  [[nodiscard]] std::optional<SectionType> resolve_hook(std::string_view name) const;
  [[nodiscard]] std::optional<VarType>     resolve_var_type(std::string_view name) const;
  [[nodiscard]] const MapParams           *get_operator_params(std::string_view prefix) const;
  [[nodiscard]] const MapParams           *get_condition_params(std::string_view prefix) const;

private:
  [[nodiscard]] ResolveResult resolve_in_table(std::string_view symbol, const std::unordered_map<std::string, MapParams> &table,
                                               SectionType section) const;

  std::unordered_map<std::string, MapParams>   _operator_map;
  std::unordered_map<std::string, MapParams>   _condition_map;
  std::unordered_map<std::string, MapParams>   _function_map;
  std::unordered_map<std::string, MapParams>   _statement_function_map;
  std::unordered_map<std::string, SectionType> _hook_map;
  std::unordered_map<std::string, VarType>     _var_type_map;
};

[[nodiscard]] const SymbolResolver &symbol_resolver();

} // namespace hrw4u
