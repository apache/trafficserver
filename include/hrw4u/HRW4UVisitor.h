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
#include <vector>
#include <stack>
#include <map>
#include <memory>
#include <optional>

#include "hrw4u/Types.h"
#include "hrw4u/Error.h"
#include "hrw4u/Tables.h"
#include "hrw4u/Visitor.h"

namespace hrw4u
{

class HRW4UVisitorImpl;

class HRW4UVisitor
{
public:
  HRW4UVisitor(const FactoryCallbacks &callbacks, const ParserConfig &config);
  ~HRW4UVisitor();

  HRW4UVisitor(const HRW4UVisitor &)            = delete;
  HRW4UVisitor &operator=(const HRW4UVisitor &) = delete;
  HRW4UVisitor(HRW4UVisitor &&) noexcept;
  HRW4UVisitor &operator=(HRW4UVisitor &&) noexcept;

  ParseResult parse(std::string_view input);
  ParseResult parse_file(std::string_view filename);

  [[nodiscard]] bool                  has_errors() const;
  [[nodiscard]] const ErrorCollector &errors() const;

private:
  std::unique_ptr<HRW4UVisitorImpl> _impl;
};

enum class ModifierType { CONDITION, OPERATOR, UNKNOWN };

struct ModifierInfo {
  std::string  name;
  ModifierType type = ModifierType::UNKNOWN;

  static ModifierInfo parse(std::string_view mod);
  static bool         is_condition_modifier(std::string_view mod);
  static bool         is_operator_modifier(std::string_view mod);
};

struct CondState {
  bool not_modifier    = false;
  bool or_modifier     = false;
  bool and_modifier    = false;
  bool last_modifier   = false;
  bool nocase_modifier = false;
  bool ext_modifier    = false;
  bool pre_modifier    = false;

  void reset();
  void add_modifier(std::string_view mod);

  [[nodiscard]] std::vector<std::string> to_list() const;
  [[nodiscard]] std::string              render_suffix() const;
  [[nodiscard]] CondState                copy() const;
};

struct OperatorState {
  bool last_modifier = false;
  bool qsa_modifier  = false;
  bool inv_modifier  = false;

  void reset();
  void add_modifier(std::string_view mod);

  [[nodiscard]] std::vector<std::string> to_list() const;
  [[nodiscard]] std::string              render_suffix() const;
};

} // namespace hrw4u
