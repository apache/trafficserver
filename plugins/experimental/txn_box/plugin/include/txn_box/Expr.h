/**
  @section license License

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

  Copyright 2019, Oath Inc.
*/

#pragma once

#include <string_view>
#include <variant>

#include <swoc/bwf_base.h>

#include "txn_box/Modifier.h"
#include "txn_box/Extractor.h"

/// Parsed feature expression.
class Expr
{
  using self_type = Expr;            ///< Self reference type.
  using Spec      = Extractor::Spec; ///< Import for convenience.
public:
  /// Output generator for BWF on an expression.
  class bwf_ex
  {
  public:
    /// Construct with specifier sequence.
    bwf_ex(std::vector<Spec> const &specs) : _specs(specs), _iter(specs.begin()) {}

    /// Validity check.
    explicit
    operator bool() const
    {
      return _iter != _specs.end();
    }
    ///
    bool operator()(std::string_view &literal, Spec &spec);

  protected:
    std::vector<Spec> const &_specs;         ///< Specifiers in format.
    std::vector<Spec>::const_iterator _iter; ///< Current specifier.
  };

  /// Single extractor that generates a direct value.
  struct Direct {
    Direct(Spec const &spec, ActiveType rtype) : _spec(spec), _result_type(rtype) {}
    Spec _spec;                       ///< Specifier with extractor.
    ActiveType _result_type = STRING; ///< Type of full, default is a string.
  };

  /// A composite of extractors and literals.
  /// Always a string.
  struct Composite {
    /// Specifiers / elements of the parsed format string.
    std::vector<Spec> _specs;
  };

  struct List {
    /// Expressions which are the elements of the tuple.
    std::vector<self_type> _exprs;
    ActiveType _types; ///< Types of the expressions.
  };

  using Raw = std::variant<std::monostate, Feature, Direct, Composite, List>;
  /// Concrete types for a specific expression.
  Raw _raw;
  /// Enumerations for type indices.
  enum {
    /// No value, uninitialized.
    NO_EXPR = 0,
    /// Literal value, stored in a Feature. No extraction needed.
    LITERAL = 1,
    /// A single extractor, directly accessed to get a Feature.
    DIRECT = 2,
    /// String value composed of multiple literals and/or extractors.
    COMPOSITE = 3,
    /// Nested expression - this expression is a sequence of other expressions.
    LIST = 4
  };

  ///< Largest argument index. -1 => no numbered arguments.
  int _max_arg_idx = -1;

  /// Post extraction modifiers.
  std::vector<Modifier::Handle> _mods;

  Expr()                                      = default;
  Expr(self_type const &that)                 = delete;
  Expr(self_type &&that)                      = default;
  self_type &operator=(self_type const &that) = delete;
  self_type &operator=(self_type &&that)      = default;

  /** Construct from a Feature.
   *
   * @param f Feature that is the result of the expression.
   *
   * The constructed instance will always be the literal @a f.
   */
  Expr(Feature const &f) : _raw(f) {}
  Expr(Direct &&d) : _raw(std::move(d)) {}
  Expr(Composite &&comp) : _raw(std::move(comp)) {}

  /** Construct @c DIRECT
   *
   * @param spec Specifier
   * @param t Result type of expression.
   */
  Expr(Spec const &spec, ActiveType t)
  {
    _raw.emplace<DIRECT>(spec, t);
    _max_arg_idx = spec._idx;
  }

  ActiveType
  result_type() const
  {
    struct Visitor {
      ActiveType
      operator()(std::monostate const &)
      {
        return {};
      }
      ActiveType
      operator()(Feature const &f)
      {
        return f.active_type();
      }
      ActiveType
      operator()(Direct const &d)
      {
        return d._result_type;
      }
      ActiveType
      operator()(Composite const &)
      {
        return STRING;
      }
      ActiveType
      operator()(List const &l)
      {
        return ActiveType{ActiveType::TupleOf(l._types.base_types())};
      }
    };
    ActiveType zret = std::visit(Visitor{}, _raw);
    for (auto const &mod : _mods) {
      zret = mod->result_type(zret);
    }
    return zret;
  }

  bool
  empty() const
  {
    return _raw.index() == NO_EXPR;
  }
  bool
  is_null() const
  {
    return _raw.index() == LITERAL && std::get<LITERAL>(_raw).value_type() == NIL;
  }
  bool
  is_literal() const
  {
    return _raw.index() == LITERAL;
  }

  struct bwf_visitor {
    bwf_visitor(Context &ctx) : _ctx(ctx) {}

    Feature
    operator()(std::monostate const &)
    {
      return NIL_FEATURE;
    }
    Feature
    operator()(Feature const &f)
    {
      return f;
    }
    Feature
    operator()(Direct const &d)
    {
      return d._spec._exf->extract(_ctx, d._spec);
    }
    Feature operator()(Composite const &comp);
    Feature operator()(List const &list);

    Context &_ctx;
  };
};
