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

#include <string>
#include <algorithm>

#include <swoc/bwf_base.h>

#include "txn_box/common.h"
#include "txn_box/Rxp.h"
#include "txn_box/Comparison.h"
#include "txn_box/Directive.h"
#include "txn_box/Config.h"
#include "txn_box/Context.h"

using swoc::TextView;
using namespace swoc::literals;
using swoc::Errata;
using swoc::Rv;

Comparison::Factory Comparison::_factory;

unsigned
Comparison::rxp_group_count() const
{
  return 0;
}

Errata
Comparison::define(swoc::TextView name, ActiveType const &types, Comparison::Loader &&worker)
{
  _factory[name] = std::make_tuple(std::move(worker), types);
  return {};
}

bool
Comparison::operator()(Context &ctx, Generic const *g) const
{
  Feature f{g->extract()};
  return IndexFor(GENERIC) == f.index() ? false : (*this)(ctx, f);
}

Rv<Comparison::Handle>
Comparison::load(Config &cfg, YAML::Node node)
{
  if (!node.IsMap()) {
    return Errata(S_ERROR, "Comparison at {} is not an object.", node.Mark());
  }

  for (auto const &[key_node, value_node] : node) {
    TextView key{key_node.Scalar()};
    auto &&[arg, arg_errata]{parse_arg(key)};
    if (!arg_errata.is_ok()) {
      return std::move(arg_errata);
    }
    if (key == Global::DO_KEY) {
      continue;
    }
    // See if this is in the factory. It's not an error if it's not, to enable adding extra
    // keys to comparison. First key that is in the factory determines the comparison type.
    if (auto spot{_factory.find(key)}; spot != _factory.end()) {
      auto &&[loader, types] = spot->second;
      if (!cfg.active_type().can_satisfy(types)) {
        return Errata(S_ERROR, R"(Comparison "{}" at {} is not valid for active feature.)", key, node.Mark());
      }

      auto &&[handle, errata]{loader(cfg, node, key, arg, value_node)};
      if (!errata.is_ok()) {
        return std::move(errata);
      }

      return std::move(handle);
    }
  }
  return Errata(S_ERROR, R"(No valid comparison key in object at {}.)", node.Mark());
}

void
Comparison::can_accelerate(Accelerator::Counters &) const
{
}

void
Comparison::accelerate(StringAccelerator *) const
{
}
/* ------------------------------------------------------------------------------------ */
class Cmp_otherwise : public Comparison
{
  using self_type  = Cmp_otherwise; ///< Self reference type.
  using super_type = Comparison;    ///< Parent type.
public:
  static const std::string KEY; ///< Comparison name.
  static const ValueMask TYPES; ///< Supported types.

  using Comparison::operator(); // declare hidden member function
  bool operator()(Context &ctx, Feature const &feature) const override;

  /// Construct an instance from YAML configuration.
  static Rv<Handle> load(Config &, YAML::Node const &, TextView const &, TextView const &, YAML::Node);

protected:
  Cmp_otherwise() = default;
};

const std::string Cmp_otherwise::KEY{"otherwise"};
const ValueMask Cmp_otherwise::TYPES{ValueMask{}.set()};

bool
Cmp_otherwise::operator()(Context &, Feature const &) const
{
  return true;
}
Rv<Comparison::Handle>
Cmp_otherwise::load(Config &, YAML::Node const &, TextView const &, TextView const &, YAML::Node)
{
  return {Handle{new self_type}, {}};
}
/* ------------------------------------------------------------------------------------ */
/** Utility base class for comparisons that are based on literal string matching.
 * This is @b not intended to be used as a comparison itself.
 */
class Cmp_String : public Comparison
{
  using self_type  = Cmp_String; ///< Self reference type.
  using super_type = Comparison; ///< Parent type.
public:
  static constexpr TextView NO_CASE_OPT{"nc"};

protected:
  union Options {
    unsigned int all;
    struct {
      unsigned int nc : 1;
    } f;
    Options() { all = 0; } // Force zero initialization.
  };

  static Rv<Options> parse_options(TextView options);
};

Rv<Cmp_String::Options>
Cmp_String::parse_options(TextView options)
{
  Options zret;
  while (options) {
    auto token = options.take_prefix_at(',');
    if (0 == strcasecmp(NO_CASE_OPT, token)) {
      zret.f.nc = true;
    } else {
      return Errata(S_ERROR, R"("{}" is not a valid option for a string comparison.)", token);
    }
  }
  return zret;
}

// ---
// Specialized subclasses for the various options.

/// Base case for literal string comparisons.
class Cmp_LiteralString : public Cmp_String
{
public:
  static constexpr TextView MATCH_KEY{"match"};
  static constexpr TextView CONTAIN_KEY{"contains"};
  static constexpr TextView PREFIX_KEY{"prefix"};
  static constexpr TextView SUFFIX_KEY{"suffix"};
  static constexpr TextView TLD_KEY{"tld"};
  static constexpr TextView PATH_KEY{"path"};

  /// Mark for @c STRING support only.
  static const ActiveType TYPES;

  /** Compare @a text for a match.
   *
   * @param ctx The transaction context.
   * @param text The feature to compare.
   * @return @c true if @a text matches, @c false otherwise.
   *
   * External API - required as a @c Comparison.
   */
  bool operator()(Context &ctx, feature_type_for<STRING> const &text) const override;

  /** Instantiate an instance from YAML configuration.
   *
   * @param cfg Global configuration object.
   * @param cmp_node The node containing the comparison.
   * @param key Key for comparison.
   * @param arg Argument for @a key, if any (stripped from @a key).
   * @param value_node Value node for for @a key.
   * @return An instance or errors on failure.
   */
  static Rv<Handle> load(Config &cfg, YAML::Node const &cmp_node, TextView const &key, TextView const &arg, YAML::Node value_node);

protected:
  Expr _expr; ///< To be compared to active feature.

  Cmp_LiteralString(Expr &&expr);

  using Comparison::operator(); // declare hidden member function
  /** Specialized comparison.
   *
   * @param ctx Runtime context.
   * @param text Configured value to check against.
   * @param active Runtime value to check.
   * @return @c true on match @c false otherwise.
   *
   * This class will handle extracting the stored expression and pass it piecewise (if needed)
   * to the specialized subclass. @a text is the extracted text, @a active is the value passed
   * in at run time to check.
   */
  virtual bool operator()(Context &ctx, TextView const &text, TextView active) const = 0;

  struct expr_validator {
    bool
    operator()(std::monostate const &)
    {
      return false;
    }
    bool
    operator()(Feature const &f)
    {
      return f.value_type() == STRING;
    }
    bool
    operator()(Expr::Direct const &d)
    {
      return d._result_type.can_satisfy(STRING);
    }
    bool
    operator()(Expr::Composite const &)
    {
      return true;
    }
    bool
    operator()(Expr::List const &list)
    {
      return list._types.can_satisfy(STRING);
    }
  };
};

const ActiveType Cmp_LiteralString::TYPES{STRING, ActiveType::TupleOf(STRING)};

Cmp_LiteralString::Cmp_LiteralString(Expr &&expr) : _expr(std::move(expr)) {}

bool
Cmp_LiteralString::operator()(Context &ctx, feature_type_for<STRING> const &feature) const
{
  Feature f{ctx.extract(_expr)};
  if (auto view = std::get_if<IndexFor(STRING)>(&f); nullptr != view) {
    return (*this)(ctx, *view, feature);
  } else if (auto t = std::get_if<IndexFor(TUPLE)>(&f); nullptr != t) {
    return std::any_of(t->begin(), t->end(), [&](Feature &f) -> bool {
      auto view = std::get_if<IndexFor(STRING)>(&f);
      return view && (*this)(ctx, *view, feature);
    });
  }
  return false;
}

/// Match entire string.
class Cmp_MatchStd : public Cmp_LiteralString
{
protected:
  using self_type  = Cmp_MatchStd;
  using super_type = Cmp_LiteralString;
  using super_type::super_type;
  using Comparison::operator(); // declare hidden member function
  bool operator()(Context &ctx, TextView const &text, TextView active) const override;

  friend super_type;
};

bool
Cmp_MatchStd::operator()(Context &ctx, TextView const &text, TextView active) const
{
  if (text == active) {
    ctx.set_literal_capture(active);
    ctx._remainder.clear();
    return true;
  }
  return false;
}

/// Match entire string, ignoring case
class Cmp_MatchNC : public Cmp_LiteralString
{
protected:
  using self_type  = Cmp_MatchNC;
  using super_type = Cmp_LiteralString;
  using super_type::super_type;
  using Comparison::operator(); // declare hidden member function
  bool operator()(Context &ctx, TextView const &text, TextView active) const override;

  friend super_type;
};

bool
Cmp_MatchNC::operator()(Context &ctx, TextView const &text, TextView active) const
{
  if (0 == strcasecmp(text, active)) {
    ctx.set_literal_capture(active);
    ctx._remainder.clear();
    return true;
  }
  return false;
}

/// Compare the active feature to a string suffix.
class Cmp_Suffix : public Cmp_LiteralString
{
protected:
  using self_type  = Cmp_Suffix;
  using super_type = Cmp_LiteralString;
  using super_type::super_type;
  using Comparison::operator(); // declare hidden member function
  bool operator()(Context &ctx, TextView const &text, TextView active) const override;

  friend super_type;
};

bool
Cmp_Suffix::operator()(Context &ctx, TextView const &text, TextView active) const
{
  if (active.ends_with(text)) {
    ctx.set_literal_capture(active.suffix(text.size()));
    ctx._remainder = active.remove_suffix(text.size());
    return true;
  }
  return false;
}

/// Compare without case the active feature to a string suffix.
class Cmp_SuffixNC : public Cmp_LiteralString
{
protected:
  using self_type  = Cmp_SuffixNC;
  using super_type = Cmp_LiteralString;
  using super_type::super_type;
  using Comparison::operator(); // declare hidden member function
  bool operator()(Context &ctx, TextView const &text, TextView active) const override;

  friend super_type;
};

bool
Cmp_SuffixNC::operator()(Context &ctx, TextView const &text, TextView active) const
{
  if (active.ends_with_nocase(text)) {
    ctx.set_literal_capture(active.suffix(text.size()));
    ctx._remainder = active.remove_suffix(text.size());
    return true;
  }
  return false;
}

class Cmp_Prefix : public Cmp_LiteralString
{
protected:
  using self_type  = Cmp_Prefix;
  using super_type = Cmp_LiteralString;
  using super_type::super_type;
  using Comparison::operator(); // declare hidden member function
  bool operator()(Context &ctx, TextView const &text, TextView active) const override;

  friend super_type;
};

bool
Cmp_Prefix::operator()(Context &ctx, TextView const &text, TextView active) const
{
  if (active.starts_with(text)) {
    ctx.set_literal_capture(active.prefix(text.size()));
    ctx._remainder = active.remove_prefix(text.size());
    return true;
  }
  return false;
}

class Cmp_PrefixNC : public Cmp_LiteralString
{
protected:
  using self_type  = Cmp_PrefixNC;
  using super_type = Cmp_LiteralString;
  using super_type::super_type;
  using Comparison::operator(); // declare hidden member function
  bool operator()(Context &ctx, TextView const &text, TextView active) const override;

  friend super_type;
};

bool
Cmp_PrefixNC::operator()(Context &ctx, TextView const &text, TextView active) const
{
  if (active.starts_with_nocase(text)) {
    ctx.set_literal_capture(active.prefix(text.size()));
    ctx._remainder = active.remove_prefix(text.size());
    return true;
  }
  return false;
}

class Cmp_Contains : public Cmp_LiteralString
{
protected:
  using self_type  = Cmp_Contains;
  using super_type = Cmp_LiteralString;
  using super_type::super_type;
  using Comparison::operator(); // declare hidden member function
  bool operator()(Context &ctx, TextView const &text, TextView active) const override;

  friend super_type;
};

bool
Cmp_Contains::operator()(Context &ctx, TextView const &text, TextView active) const
{
  if (auto idx = active.find(text); idx != TextView::npos) {
#if 0
    if (ctx._update_remainder_p) {
      auto n = active.size() - text.size();
      auto span = ctx._arena->alloc(n).rebind<char>();
      memcpy(span, active.prefix(idx));
      memcpy(span.data() + idx, active.suffix(idx).data(), active.size() - (idx + text.size()));
      ctx.set_literal_capture(span.view());
    }
#else
    ctx._remainder.clear();
#endif
    return true;
  }
  return false;
}

class Cmp_ContainsNC : public Cmp_LiteralString
{
protected:
  using self_type  = Cmp_ContainsNC;
  using super_type = Cmp_LiteralString;
  using super_type::super_type;
  using Comparison::operator(); // declare hidden member function
  bool operator()(Context &ctx, TextView const &text, TextView active) const override;

  friend super_type;
};

bool
Cmp_ContainsNC::operator()(Context &ctx, TextView const &text, TextView active) const
{
  if (text.size() <= active.size()) {
    auto spot = std::search(active.begin(), active.end(), text.begin(), text.end(),
                            [](char lhs, char rhs) { return tolower(lhs) == tolower(rhs); });
    if (spot != active.end()) {
#if 0
      if (ctx._update_remainder_p) {
        size_t idx = spot - active.begin();
        auto n = active.size() - text.size();
        auto span = ctx._arena->alloc(n).rebind<char>();
        memcpy(span, active.prefix(idx));
        memcpy(span.data() + idx, active.suffix(idx).data(), active.size() - (idx + text.size()));
        ctx.set_literal_capture(span.view());
      }
#else
      ctx._remainder.clear();
#endif
      return true;
    }
  }
  return false;
}

class Cmp_TLD : public Cmp_LiteralString
{
protected:
  using self_type  = Cmp_TLD;
  using super_type = Cmp_LiteralString;
  using super_type::super_type;
  using Comparison::operator(); // declare hidden member function
  bool operator()(Context &ctx, TextView const &text, TextView active) const override;

  friend super_type;
};

bool
Cmp_TLD::operator()(Context &ctx, TextView const &text, TextView active) const
{
  if (active.ends_with(text) && (text.size() == active.size() || active[active.size() - text.size() - 1] == '.')) {
    auto capture = active.suffix(text.size() + 1);
    ctx.set_literal_capture(capture);
    ctx._remainder = active.prefix(active.size() - capture.size());
    return true;
  }
  return false;
}

class Cmp_TLDNC : public Cmp_LiteralString
{
protected:
  using self_type  = Cmp_TLDNC;
  using super_type = Cmp_LiteralString;
  using super_type::super_type;
  using Comparison::operator(); // declare hidden member function
  bool operator()(Context &ctx, TextView const &text, TextView active) const override;

  friend super_type;
};

bool
Cmp_TLDNC::operator()(Context &ctx, TextView const &text, TextView active) const
{
  if (active.ends_with_nocase(text) && (text.size() == active.size() || active[active.size() - text.size() - 1] == '.')) {
    auto capture = active.suffix(text.size() + 1);
    ctx.set_literal_capture(capture);
    ctx._remainder = active.prefix(active.size() - capture.size());
    return true;
  }
  return false;
}

// ---

class Cmp_Path : public Cmp_LiteralString
{
protected:
  using self_type  = Cmp_Path;
  using super_type = Cmp_LiteralString;
  using super_type::super_type;
  using Comparison::operator(); // declare hidden member function
  bool operator()(Context &ctx, TextView const &text, TextView active) const override;

  friend super_type;
};

bool
Cmp_Path::operator()(Context &ctx, TextView const &text, TextView active) const
{
  auto target = text;
  target.rtrim('/'); // Not reliable to do at load, since not always a config time constant.
  if (active.starts_with(target)) {
    auto rest = active.substr(target.size());
    if (rest.empty() || rest == "/"_tv) {
      auto n = target.size() + rest.size();
      ctx.set_literal_capture(active.prefix(n));
      ctx._remainder = active.substr(n);
      return true;
    }
  }
  return false;
}

class Cmp_PathNC : public Cmp_LiteralString
{
protected:
  using self_type  = Cmp_PathNC;
  using super_type = Cmp_LiteralString;
  using super_type::super_type;
  using Comparison::operator(); // declare hidden member function
  bool operator()(Context &ctx, TextView const &text, TextView active) const override;

  friend super_type;
};

bool
Cmp_PathNC::operator()(Context &ctx, TextView const &text, TextView active) const
{
  if (active.starts_with_nocase(text)) {
    auto rest = active.substr(text.size());
    if (rest.empty() || rest == "/"_tv) {
      auto n = text.size() + rest.size();
      ctx.set_literal_capture(active.prefix(n));
      ctx._remainder = active.substr(n);
      return true;
    }
  }
  return false;
}

// ---

Rv<Comparison::Handle>
Cmp_LiteralString::load(Config &cfg, YAML::Node const &cmp_node, TextView const &key, TextView const &arg, YAML::Node value_node)
{
  auto &&[expr, errata]{cfg.parse_expr(value_node)};

  if (!errata.is_ok()) {
    errata.note(R"(While parsing comparison "{}" at {}.)", key, cmp_node.Mark());
    return std::move(errata);
  }

  auto &&[options, opt_errata]{parse_options(arg)};
  if (!opt_errata.is_ok()) {
    opt_errata.note(R"(While parsing argument "{}" for comparison "{}".)", arg, key);
    return std::move(opt_errata);
  }

  auto expr_type = expr.result_type();
  if (!expr_type.can_satisfy(TYPES)) {
    return Errata(S_ERROR, R"(Value type "{}" for comparison "{}" at {} is not supported.)", expr_type, key, cmp_node.Mark());
  }

  if (MATCH_KEY == key) {
    return options.f.nc ? Handle{new Cmp_MatchNC(std::move(expr))} : Handle{new Cmp_MatchStd(std::move(expr))};
  } else if (PREFIX_KEY == key) {
    return options.f.nc ? Handle{new Cmp_PrefixNC(std::move(expr))} : Handle{new Cmp_Prefix(std::move(expr))};
  } else if (SUFFIX_KEY == key) {
    return options.f.nc ? Handle{new Cmp_SuffixNC(std::move(expr))} : Handle{new Cmp_Suffix(std::move(expr))};
  } else if (CONTAIN_KEY == key) {
    return options.f.nc ? Handle(new Cmp_Contains(std::move(expr))) : Handle(new Cmp_ContainsNC(std::move(expr)));
  } else if (TLD_KEY == key) {
    return options.f.nc ? Handle(new Cmp_TLDNC(std::move(expr))) : Handle(new Cmp_TLD(std::move(expr)));
  } else if (PATH_KEY == key) {
    return options.f.nc ? Handle(new Cmp_PathNC(std::move(expr))) : Handle(new Cmp_Path(std::move(expr)));
  }

  return Errata(S_ERROR, R"(Internal error, unrecognized key "{}".)", key);
}
/* ------------------------------------------------------------------------------------ */
class Cmp_Rxp : public Cmp_String
{
  using self_type  = Cmp_Rxp;    ///< Self reference type.
  using super_type = Cmp_String; ///< Super type.

public:
  static constexpr TextView KEY{"rxp"}; ///< YAML key.
  static const ActiveType TYPES;        ///< Valid comparison types.

  static Rv<Comparison::Handle> load(Config &cfg, YAML::Node const &cmp_node, TextView const &key, TextView const &arg,
                                     YAML::Node value_node);

protected:
  /// Static value - a literal or a dynamic regular expression.
  /// @see rxp_visitor
  using Item = std::variant<Rxp, Expr>;

  /// Process the comparison based on the expression type.
  /// This is used during configuration load.
  struct expr_visitor {
    /** Constructor.
     *
     * @param cfg Configuration being loaded.
     * @param opt Options from directive arguments.
     */
    expr_visitor(Config &cfg, Rxp::Options opt) : _cfg(cfg), _rxp_opt(opt) {}

    Rv<Handle> operator()(std::monostate);
    Rv<Handle> operator()(Feature &f);
    Rv<Handle> operator()(Expr::List &l);
    Rv<Handle> operator()(Expr::Direct &d);
    Rv<Handle> operator()(Expr::Composite &comp);

    Config &_cfg;          ///< Configuration being loaded.
    Rxp::Options _rxp_opt; ///< Any options from directive arguments.
  };

  /** Handlers for @c Item.
   *
   * This enables dynamic regular expressions at a reasonable run time cost. If the configuration
   * is a literal it is compiled during configuration load and stored as an @c Rxp instance.
   * Otherwise the @c Expr is stored and evaluated on invocation.
   */
  struct rxp_visitor {
    /** Invoke the @a rxp against the active feature.
     *
     * @param rxp Compiled regular expression.
     * @return @c true on success, @c false otherwise.
     */
    bool operator()(Rxp const &rxp);
    /** Compile the @a expr into a regular expression.
     *
     * @param expr Feature expression.
     * @return @c true on successful match, @c false otherwise.
     *
     * @internal This compiles the feature from @a expr and then invokes the @c Rxp overload to do
     * the match.
     */
    bool operator()(Expr const &expr);

    Context &_ctx;         ///< Configuration context.
    Rxp::Options _rxp_opt; ///< Options for the regex.
    TextView _src;         ///< regex text.
  };
};

class Cmp_RxpSingle : public Cmp_Rxp
{
  using self_type  = Cmp_RxpSingle;
  using super_type = Cmp_Rxp;

public:
  using Comparison::operator(); // declare hidden member function
  Cmp_RxpSingle(Expr &&expr, Rxp::Options);
  Cmp_RxpSingle(Rxp &&rxp);

protected:
  bool operator()(Context &ctx, feature_type_for<STRING> const &active) const override;

  Item _rxp;
  Rxp::Options _opt;
};

class Cmp_RxpList : public Cmp_Rxp
{
  using self_type  = Cmp_RxpList;
  using super_type = Cmp_Rxp;
  friend super_type;

public:
  Cmp_RxpList(Rxp::Options opt) : _opt(opt) {}

protected:
  struct expr_visitor {
    Errata operator()(Feature &f);
    Errata
    operator()(Expr::List &)
    {
      return Errata(S_ERROR, "Invalid type");
    }
    Errata
    operator()(Expr::Direct &d)
    {
      _rxp.emplace_back(Expr{std::move(d)});
      return {};
    }
    Errata
    operator()(Expr::Composite &comp)
    {
      _rxp.emplace_back(Expr{std::move(comp)});
      return {};
    }
    Errata
    operator()(std::monostate)
    {
      return Errata(S_ERROR, "Invalid type");
    }

    Rxp::Options _rxp_opt;
    std::vector<Item> &_rxp;
  };

  using Comparison::operator(); // declare hidden member function
  bool operator()(Context &ctx, feature_type_for<STRING> const &active) const override;

  std::vector<Item> _rxp;
  Rxp::Options _opt;
};

Errata
Cmp_RxpList::expr_visitor::operator()(Feature &f)
{
  if (IndexFor(STRING) != f.index()) {
    return Errata(S_ERROR, R"("{}" literal must be a string.)", KEY);
  }

  auto &&[rxp, rxp_errata]{Rxp::parse(std::get<IndexFor(STRING)>(f), _rxp_opt)};
  if (!rxp_errata.is_ok()) {
    rxp_errata.note(R"(While parsing feature expression for "{}" comparison.)", KEY);
    return std::move(rxp_errata);
  }
  _rxp.emplace_back(std::move(rxp));
  return {};
}

const ActiveType Cmp_Rxp::TYPES{STRING, ActiveType::TupleOf(STRING)};

bool
Cmp_Rxp::rxp_visitor::operator()(const Rxp &rxp)
{
  auto result = rxp(_src, _ctx.rxp_working_match_data());
  if (result > 0) {
    _ctx.rxp_commit_match(_src);
    _ctx._remainder.clear();
    return true;
  }
  return false;
}

bool
Cmp_Rxp::rxp_visitor::operator()(Expr const &expr)
{
  auto f = _ctx.extract(expr);
  if (auto text = std::get_if<IndexFor(STRING)>(&f); text != nullptr) {
    auto &&[rxp, rxp_errata]{Rxp::parse(*text, _rxp_opt)};
    if (rxp_errata.is_ok()) {
      _ctx.rxp_match_require(rxp.capture_count());
      return (*this)(rxp); // forward to Rxp overload.
    }
  }
  return false;
}

Rv<Comparison::Handle>
Cmp_Rxp::expr_visitor::operator()(Feature &f)
{
  if (IndexFor(STRING) != f.index()) {
    return Errata(S_ERROR, R"("{}" literal must be a string.)", KEY);
  }

  auto &&[rxp, rxp_errata]{Rxp::parse(std::get<IndexFor(STRING)>(f), _rxp_opt)};
  if (!rxp_errata.is_ok()) {
    rxp_errata.note(R"(While parsing feature expression for "{}" comparison.)", KEY);
    return std::move(rxp_errata);
  }
  _cfg.require_rxp_group_count(rxp.capture_count());
  return Handle(new Cmp_RxpSingle(std::move(rxp)));
}

Rv<Comparison::Handle>
Cmp_Rxp::expr_visitor::operator()(std::monostate)
{
  return Errata(S_ERROR, R"(Literal must be a string)");
}

Rv<Comparison::Handle>
Cmp_Rxp::expr_visitor::operator()(Expr::Direct &d)
{
  return Handle(new Cmp_RxpSingle(Expr{std::move(d)}, _rxp_opt));
}

Rv<Comparison::Handle>
Cmp_Rxp::expr_visitor::operator()(Expr::Composite &comp)
{
  return Handle(new Cmp_RxpSingle(Expr{std::move(comp)}, _rxp_opt));
}

Rv<Comparison::Handle>
Cmp_Rxp::expr_visitor::operator()(Expr::List &l)
{
  auto rxm = new Cmp_RxpList{_rxp_opt};
  Cmp_RxpList::expr_visitor ev{_rxp_opt, rxm->_rxp};
  for (Expr &elt : l._exprs) {
    if (!elt.result_type().can_satisfy(STRING)) {
      return Errata(S_ERROR, R"("{}" literal must be a string.)", KEY);
    }
    std::visit(ev, elt._raw);
  }
  return Handle{rxm};
}

Rv<Comparison::Handle>
Cmp_Rxp::load(Config &cfg, YAML::Node const &cmp_node, TextView const &key, TextView const &arg, YAML::Node value_node)
{
  auto &&[expr, errata]{cfg.parse_expr(value_node)};

  if (!errata.is_ok()) {
    errata.note(R"(While parsing comparison "{}" at {}.)", key, cmp_node.Mark());
    return std::move(errata);
  }

  auto &&[options, opt_errata]{self_type::parse_options(arg)};
  if (!opt_errata.is_ok()) {
    opt_errata.note(R"(While parsing argument "{}" for comparison "{}".)", arg, key);
    return std::move(opt_errata);
  }

  Rxp::Options rxp_opt;
  rxp_opt.f.nc = options.f.nc;
  return std::visit(expr_visitor{cfg, rxp_opt}, expr._raw);
}

Cmp_RxpSingle::Cmp_RxpSingle(Expr &&expr, Rxp::Options opt) : _rxp(std::move(expr)), _opt(opt) {}

Cmp_RxpSingle::Cmp_RxpSingle(Rxp &&rxp) : _rxp(std::move(rxp)) {}

bool
Cmp_RxpSingle::operator()(Context &ctx, feature_type_for<STRING> const &active) const
{
  return std::visit(rxp_visitor{ctx, _opt, active}, _rxp);
}

bool
Cmp_RxpList::operator()(Context &ctx, feature_type_for<STRING> const &) const
{
  return std::any_of(_rxp.begin(), _rxp.end(), [&](Item const &item) { return std::visit(rxp_visitor{ctx, _opt, {}}, item); });
}

/* ------------------------------------------------------------------------------------ */
/** Compare a boolean value.
 * Check if a value is true.
 */
class Cmp_is_true : public Comparison
{
  using self_type  = Cmp_is_true; ///< Self reference type.
  using super_type = Comparison;  ///< Parent type.
public:
  static inline const std::string KEY{"is-true"};                              ///< Comparison name.
  static inline const ValueMask TYPES{MaskFor(NIL, STRING, BOOLEAN, INTEGER)}; ///< Supported types.

  using Comparison::operator(); // declare hidden member function
  bool operator()(Context &ctx, Feature const &feature) const override;

  /// Construct an instance from YAML configuration.
  static Rv<Handle> load(Config &, YAML::Node const &, TextView const &, TextView const &, YAML::Node);

protected:
  Cmp_is_true() = default;
};

bool
Cmp_is_true::operator()(Context &, Feature const &feature) const
{
  return feature.as_bool();
}

Rv<Comparison::Handle>
Cmp_is_true::load(Config &, YAML::Node const &, TextView const &, TextView const &, YAML::Node)
{
  return {Handle{new self_type}, {}};
}

/** Compare a boolean value.
 * Check if a value is false.
 */
class Cmp_is_false : public Comparison
{
  using self_type  = Cmp_is_false; ///< Self reference type.
  using super_type = Comparison;   ///< Parent type.
public:
  static const std::string KEY; ///< Comparison name.
  static const ValueMask TYPES; ///< Supported types.

  using Comparison::operator(); // declare hidden member function
  bool operator()(Context &ctx, Feature const &feature) const override;

  /// Construct an instance from YAML configuration.
  static Rv<Handle> load(Config &, YAML::Node const &, TextView const &, TextView const &, YAML::Node);

protected:
  Cmp_is_false() = default;
};

const std::string Cmp_is_false::KEY{"is-false"};
const ValueMask Cmp_is_false::TYPES{MaskFor({STRING, BOOLEAN, INTEGER})};

bool
Cmp_is_false::operator()(Context &, Feature const &feature) const
{
  return !feature.as_bool();
}

Rv<Comparison::Handle>
Cmp_is_false::load(Config &, YAML::Node const &, TextView const &, TextView const &, YAML::Node)
{
  return {Handle{new self_type}, {}};
}

/* ------------------------------------------------------------------------------------ */
/** Check for NULL value.
 */
class Cmp_is_null : public Comparison
{
  using self_type  = Cmp_is_null; ///< Self reference type.
  using super_type = Comparison;  ///< Parent type.
public:
  static const std::string KEY; ///< Comparison name.
  static const ValueMask TYPES; ///< Supported types.
  using Comparison::operator(); // declare hidden member function
  bool operator()(Context &ctx, feature_type_for<NIL>) const override;

  /// Construct an instance from YAML configuration.
  static Rv<Handle> load(Config &, YAML::Node const &, TextView const &, TextView const &, YAML::Node);

protected:
  Cmp_is_null() = default;
};

const std::string Cmp_is_null::KEY{"is-null"};
const ValueMask Cmp_is_null::TYPES{MaskFor(NIL)};

bool
Cmp_is_null::operator()(Context &, feature_type_for<NIL>) const
{
  return true;
}

Rv<Comparison::Handle>
Cmp_is_null::load(Config &, YAML::Node const &, TextView const &, TextView const &, YAML::Node)
{
  return {Handle{new self_type}, {}};
}
/* ------------------------------------------------------------------------------------ */
/** Check for empty (NULL or empty string)
 */
class Cmp_is_empty : public Comparison
{
  using self_type  = Cmp_is_empty; ///< Self reference type.
  using super_type = Comparison;   ///< Parent type.
public:
  static const std::string KEY; ///< Comparison name.
  static const ValueMask TYPES; ///< Supported types.
  using Comparison::operator(); // declare hidden member function
  bool operator()(Context &ctx, feature_type_for<NIL>) const override;
  bool operator()(Context &ctx, feature_type_for<STRING> const &s) const override;
  bool operator()(Context &ctx, feature_type_for<TUPLE> const &s) const override;

  /// Construct an instance from YAML configuration.
  static Rv<Handle> load(Config &, YAML::Node const &, TextView const &, TextView const &, YAML::Node);

protected:
  Cmp_is_empty() = default;
};

const std::string Cmp_is_empty::KEY{"is-empty"};
const ValueMask Cmp_is_empty::TYPES{MaskFor({NIL, STRING, TUPLE})};

bool
Cmp_is_empty::operator()(Context &, feature_type_for<NIL>) const
{
  return true;
}

bool
Cmp_is_empty::operator()(Context &, feature_type_for<STRING> const &s) const
{
  return s.empty();
}

bool
Cmp_is_empty::operator()(Context &, feature_type_for<TUPLE> const &t) const
{
  return t.count() == 0;
}

Rv<Comparison::Handle>
Cmp_is_empty::load(Config &, YAML::Node const &, TextView const &, TextView const &, YAML::Node)
{
  return {Handle{new self_type}, {}};
}
/* ------------------------------------------------------------------------------------ */
/// Common elements for all binary integer comparisons.
/// @internal Verify the base comparison operators for @c Feature support these types.
struct Base_Binary_Cmp : public Comparison {
  using self_type  = Base_Binary_Cmp; ///< Self reference type.
  using super_type = Comparison;      ///< Parent type.
public:
  /// Supported types.
  using Cmp_Types = swoc::meta::type_list<feature_type_for<INTEGER>, feature_type_for<BOOLEAN>, feature_type_for<IP_ADDR>,
                                          feature_type_for<DURATION>>;

  static inline const ActiveType TYPES = Cmp_Types::apply<ValueMaskFor>::value; ///< Mask for supported types.

  /** Instantiate an instance from YAML configuration.
   *
   * @tparam Concrete type to instantiate.
   * @param cfg Global configuration object.
   * @param cmp_node The node containing the comparison.
   * @param key Key for comparison.
   * @param arg Argument for @a key, if any (stripped from @a key).
   * @param value_node Value node for for @a key.
   * @return An instance or errors on failure.
   */
  template <typename T>
  static Rv<Handle> load(Config &cfg, YAML::Node const &cmp_node, TextView const &key, TextView const &arg, YAML::Node value_node);

protected:
  Expr _expr;

  Base_Binary_Cmp(Expr &&expr) : _expr(std::move(expr)) {}
};

template <typename T>
Rv<Comparison::Handle>
Base_Binary_Cmp::load(Config &cfg, YAML::Node const &, TextView const &key, TextView const &, YAML::Node value_node)
{
  auto &&[expr, errata] = cfg.parse_expr(value_node);
  if (!errata.is_ok()) {
    return std::move(errata.note(R"(While parsing comparison "{}" value at {}.)", key, value_node.Mark()));
  }
  auto expr_type = expr.result_type();
  if (!expr_type.can_satisfy(TYPES)) {
    return Errata(S_ERROR, R"(The value is of type "{}" for "{}" at {} which is not "{}" as required.)", expr_type, key,
                  value_node.Mark(), TYPES);
  }
  return Handle(new T(std::move(expr)));
}

// --- The concrete comparisons.

struct Cmp_eq : public Base_Binary_Cmp {
  using self_type  = Cmp_eq;          ///< Self reference type.
  using super_type = Base_Binary_Cmp; ///< Parent type.
public:
  static inline const std::string KEY = "eq";
  using super_type::super_type;
  using Comparison::operator(); // declare hidden member function

  bool
  operator()(Context &ctx, Feature const &f) const override
  {
    return f == ctx.extract(_expr);
  }
  static Rv<Handle>
  load(Config &cfg, YAML::Node const &cmp_node, TextView const &key, TextView const &arg, YAML::Node value_node)
  {
    return super_type::load<self_type>(cfg, cmp_node, key, arg, value_node);
  }
};

struct Cmp_ne : public Base_Binary_Cmp {
  using self_type  = Cmp_ne;          ///< Self reference type.
  using super_type = Base_Binary_Cmp; ///< Parent type.
public:
  static inline const std::string KEY = "ne";
  using super_type::super_type;
  using Comparison::operator(); // declare hidden member function

  bool
  operator()(Context &ctx, Feature const &f) const override
  {
    return f != ctx.extract(_expr);
  }
  static Rv<Handle>
  load(Config &cfg, YAML::Node const &cmp_node, TextView const &key, TextView const &arg, YAML::Node value_node)
  {
    return super_type::load<self_type>(cfg, cmp_node, key, arg, value_node);
  }
};

struct Cmp_lt : public Base_Binary_Cmp {
  using self_type  = Cmp_lt;          ///< Self reference type.
  using super_type = Base_Binary_Cmp; ///< Parent type.
public:
  static inline const std::string KEY = "lt";
  using super_type::super_type;
  using Comparison::operator(); // declare hidden member function

  bool
  operator()(Context &ctx, Feature const &f) const override
  {
    return f < ctx.extract(_expr);
  }
  static Rv<Handle>
  load(Config &cfg, YAML::Node const &cmp_node, TextView const &key, TextView const &arg, YAML::Node value_node)
  {
    return super_type::load<self_type>(cfg, cmp_node, key, arg, value_node);
  }
};

struct Cmp_le : public Base_Binary_Cmp {
  using self_type  = Cmp_le;          ///< Self reference type.
  using super_type = Base_Binary_Cmp; ///< Parent type.
public:
  static inline const std::string KEY = "le";
  using super_type::super_type;
  using Comparison::operator(); // declare hidden member function

  bool
  operator()(Context &ctx, Feature const &f) const override
  {
    return f <= ctx.extract(_expr);
  }
  static Rv<Handle>
  load(Config &cfg, YAML::Node const &cmp_node, TextView const &key, TextView const &arg, YAML::Node value_node)
  {
    return super_type::load<self_type>(cfg, cmp_node, key, arg, value_node);
  }
};

struct Cmp_gt : public Base_Binary_Cmp {
  using self_type  = Cmp_gt;          ///< Self reference type.
  using super_type = Base_Binary_Cmp; ///< Parent type.
public:
  static inline const std::string KEY = "gt";
  using super_type::super_type;
  using Comparison::operator(); // declare hidden member function

  bool
  operator()(Context &ctx, Feature const &f) const override
  {
    return ctx.extract(_expr) < f;
  }
  static Rv<Handle>
  load(Config &cfg, YAML::Node const &cmp_node, TextView const &key, TextView const &arg, YAML::Node value_node)
  {
    return super_type::load<self_type>(cfg, cmp_node, key, arg, value_node);
  }
};

struct Cmp_ge : public Base_Binary_Cmp {
  using self_type  = Cmp_ge;          ///< Self reference type.
  using super_type = Base_Binary_Cmp; ///< Parent type.
public:
  static inline const std::string KEY = "ge";
  using super_type::super_type;
  using Comparison::operator(); // declare hidden member function

  bool
  operator()(Context &ctx, Feature const &f) const override
  {
    return ctx.extract(_expr) <= f;
  }
  static Rv<Handle>
  load(Config &cfg, YAML::Node const &cmp_node, TextView const &key, TextView const &arg, YAML::Node value_node)
  {
    return super_type::load<self_type>(cfg, cmp_node, key, arg, value_node);
  }
};

// --- //
/// Compare against a range.
class Cmp_in : public Comparison
{
  using self_type  = Cmp_in;     ///< Self reference type.
  using super_type = Comparison; ///< Parent type.
public:
  static const std::string KEY;  ///< Comparison name.
  static const ActiveType TYPES; ///< Supported types.
  using Comparison::operator();  // declare hidden member function
  bool operator()(Context &ctx, feature_type_for<INTEGER> n) const override;
  bool operator()(Context &ctx, feature_type_for<IP_ADDR> const &addr) const override;

  /// Construct an instance from YAML configuration.
  static Rv<Handle> load(Config &cfg, YAML::Node const &cmp_node, TextView const &key, TextView const &arg, YAML::Node value_node);

protected:
  Expr _min;
  Expr _max;
};

const std::string Cmp_in::KEY{"in"};
const ActiveType Cmp_in::TYPES{INTEGER, IP_ADDR};

bool
Cmp_in::operator()(Context &ctx, feature_type_for<IP_ADDR> const &addr) const
{
  auto lhs = ctx.extract(_min);
  auto rhs = ctx.extract(_max);
  return (lhs.index() == rhs.index()) && (lhs.index() == IndexFor(IP_ADDR)) && (std::get<IndexFor(IP_ADDR)>(lhs) <= addr) &&
         (addr <= std::get<IndexFor(IP_ADDR)>(rhs));
}

bool
Cmp_in::operator()(Context &ctx, feature_type_for<INTEGER> n) const
{
  auto lhs = ctx.extract(_min);
  auto rhs = ctx.extract(_max);
  return (lhs.index() == rhs.index()) && (lhs.index() == IndexFor(INTEGER)) && (std::get<IndexFor(INTEGER)>(lhs) <= n) &&
         (n <= std::get<IndexFor(INTEGER)>(rhs));
}

auto
Cmp_in::load(Config &cfg, YAML::Node const &cmp_node, TextView const &, TextView const &, YAML::Node value_node) -> Rv<Handle>
{
  auto self = new self_type;
  Handle handle{self};

  if (value_node.IsScalar()) {
    // Check if it's a valid IP range - all done.
    swoc::IPRange ip_range;
    if (ip_range.load(value_node.Scalar())) {
      if (!cfg.active_type().can_satisfy(IP_ADDR)) {
        return Errata(S_ERROR, R"("{}" at line {} cannot check values of type {} against a feature of type {}.)", KEY,
                      cmp_node.Mark(), IP_ADDR, cfg.active_type());
      }
      self->_min = Feature{ip_range.min()};
      self->_max = Feature{ip_range.max()};
      return handle;
    }

    // Need to parse and verify it's a range of integers.
    TextView max_text = value_node.Scalar();
    auto min_text     = max_text.take_prefix_at('-');
    TextView parsed;

    if (max_text.empty()) {
      return Errata(
        S_ERROR,
        R"(Value for "{}" at line {} must be two integers separated by a '-', or IP address range or network. [separate '-' not found])",
        KEY, cmp_node.Mark());
    }
    auto n_min = svtoi(min_text.trim_if(&isspace), &parsed);
    if (parsed.size() != min_text.size()) {
      return Errata(
        S_ERROR,
        R"(Value for "{}" at line {} must be two integers separated by a '-', or IP address range or network. [minimum value "{}" is not an integer])",
        KEY, cmp_node.Mark(), min_text);
    }
    auto n_max = svtoi(max_text.trim_if(&isspace), &parsed);
    if (parsed.size() != max_text.size()) {
      return Errata(
        S_ERROR,
        R"(Value for "{}" at line {} must be two integers separated by a '-', or IP address range or network. [maximum value "{}" is not an integer])",
        KEY, cmp_node.Mark(), max_text);
    }

    if (!cfg.active_type().can_satisfy(INTEGER)) {
      return Errata(S_ERROR, R"("{}" at line {} cannot check values of type {} against a feature of type {}.)", KEY,
                    cmp_node.Mark(), INTEGER, cfg.active_type());
    }

    self->_min = Feature(n_min);
    self->_max = Feature(n_max);
    return handle;
  } else if (value_node.IsSequence()) {
    if (value_node.size() == 2) {
      auto &&[lhs, lhs_errata] = cfg.parse_expr(value_node[0]);
      if (!lhs_errata.is_ok()) {
        return std::move(lhs_errata);
      }
      auto lhs_type = lhs.result_type();

      auto &&[rhs, rhs_errata] = cfg.parse_expr(value_node[1]);
      if (!rhs_errata.is_ok()) {
        return std::move(rhs_errata);
      }
      auto rhs_type = rhs.result_type();

      if (lhs_type != rhs_type) {
        return Errata(S_ERROR, R"("{}" at line {} cannot compare a range of mixed types [{}, {}].)", KEY, cmp_node.Mark(), lhs_type,
                      rhs_type);
      }

      if (!lhs_type.can_satisfy(MaskFor({INTEGER, IP_ADDR}))) {
        return Errata(S_ERROR, R"("{}" at line {} requires values of type {} or {}, not {}.)", KEY, cmp_node.Mark(), INTEGER,
                      IP_ADDR, lhs_type);
      }

      if (!cfg.active_type().can_satisfy(lhs_type)) {
        return Errata(S_ERROR, R"("{}" at line {} cannot check values of type {} against a feature of type {}.)", KEY,
                      cmp_node.Mark(), lhs_type, cfg.active_type());
      }
      self->_min = std::move(lhs);
      self->_max = std::move(rhs);
      return handle;
    } else {
      return Errata(S_ERROR, R"(The list for "{}" at line {} is not exactly 2 elements are required.)", KEY, cmp_node.Mark());
    }
  }

  return Errata(
    S_ERROR,
    R"(Value for "{}" at line {} must be a string representing an integer range, an IP address range or netowork, or list of two integers or IP addresses.)",
    KEY, cmp_node.Mark());
}
/* ------------------------------------------------------------------------------------ */
class ComboComparison : public Comparison
{
  using self_type  = ComboComparison; ///< Self reference type.
  using super_type = Comparison;      ///< Parent type.
public:
  static const ActiveType TYPES; ///< Supported types.

  virtual TextView const &key() const = 0;

  /// Construct an instance from YAML configuration.
  static Rv<std::vector<Handle>> load(Config &cfg, YAML::Node const &cmp_node, TextView const &key, TextView const &arg,
                                      YAML::Node value_node);

protected:
  /// List of sub-comparisons.
  std::vector<Handle> _cmps;

  ComboComparison(std::vector<Handle> &&cmps) : _cmps(std::move(cmps)) {}

  static Errata load_case(Config &cfg, std::vector<Handle> &cmps, YAML::Node node);
};

const ActiveType ComboComparison::TYPES{ActiveType::any_type()};

auto
ComboComparison::load(Config &cfg, YAML::Node const &cmp_node, TextView const &key, TextView const &, YAML::Node value_node)
  -> Rv<std::vector<Handle>>
{
  std::vector<Handle> cmps;
  if (value_node.IsMap()) {
    auto errata = self_type::load_case(cfg, cmps, value_node);
    if (!errata.is_ok()) {
      errata.note("While parsing {} comparison at {}.", key, cmp_node.Mark());
    }
  } else if (value_node.IsSequence()) {
    cmps.reserve(cmp_node.size());
    for (auto child : value_node) {
      auto errata = self_type::load_case(cfg, cmps, child);
      if (!errata.is_ok()) {
        errata.note("While parsing {} comparison at {}.", key, cmp_node.Mark());
        return errata;
      }
    }
  }
  return cmps;
}

Errata
ComboComparison::load_case(Config &cfg, std::vector<Handle> &cmps, YAML::Node node)
{
  auto &&[cmp_handle, cmp_errata]{Comparison::load(cfg, node)};
  if (!cmp_errata.is_ok()) {
    return std::move(cmp_errata);
  }
  cmps.emplace_back(std::move(cmp_handle));
  return {};
}

// ---

class Cmp_any_of : public ComboComparison
{
  using self_type  = Cmp_any_of;      ///< Self reference type.
  using super_type = ComboComparison; ///< Parent type.
public:
  static constexpr TextView KEY = "any-of"; ///< Comparison name.

  TextView const &
  key() const override
  {
    return KEY;
  }

  using Comparison::operator(); // declare hidden member function
  bool operator()(Context &ctx, Feature const &feature) const override;

  static Rv<Handle> load(Config &cfg, YAML::Node const &cmp_node, TextView const &key, TextView const &arg, YAML::Node value_node);

protected:
  Cmp_any_of(std::vector<Handle> &&cmps) : super_type(std::move(cmps)) {}
};

bool
Cmp_any_of::operator()(Context &ctx, Feature const &feature) const
{
  return std::any_of(_cmps.begin(), _cmps.end(), [&](Handle const &cmp) -> bool { return (*cmp)(ctx, feature); });
}

auto
Cmp_any_of::load(Config &cfg, YAML::Node const &cmp_node, TextView const &key, TextView const &arg, YAML::Node value_node)
  -> Rv<Handle>
{
  auto &&[cmps, errata] = super_type::load(cfg, cmp_node, key, arg, value_node);
  if (!errata.is_ok()) {
    return std::move(errata);
  }
  return Handle{new self_type{std::move(cmps)}};
}

// ---

class Cmp_all_of : public ComboComparison
{
  using self_type  = Cmp_all_of;      ///< Self reference type.
  using super_type = ComboComparison; ///< Parent type.
public:
  static constexpr TextView KEY = "all-of"; ///< Comparison name.

  TextView const &
  key() const override
  {
    return KEY;
  }
  using Comparison::operator(); // declare hidden member function
  bool operator()(Context &ctx, Feature const &feature) const override;

  static Rv<Handle> load(Config &cfg, YAML::Node const &cmp_node, TextView const &key, TextView const &arg, YAML::Node value_node);

protected:
  Cmp_all_of(std::vector<Handle> &&cmps) : super_type(std::move(cmps)) {}
};

bool
Cmp_all_of::operator()(Context &ctx, Feature const &feature) const
{
  return std::all_of(_cmps.begin(), _cmps.end(), [&](Handle const &cmp) -> bool { return (*cmp)(ctx, feature); });
}

auto
Cmp_all_of::load(Config &cfg, YAML::Node const &cmp_node, TextView const &key, TextView const &arg, YAML::Node value_node)
  -> Rv<Handle>
{
  auto &&[cmps, errata] = super_type::load(cfg, cmp_node, key, arg, value_node);
  if (!errata.is_ok()) {
    return std::move(errata);
  }
  return Handle{new self_type{std::move(cmps)}};
}

// ---

class Cmp_none_of : public ComboComparison
{
  using self_type  = Cmp_none_of;     ///< Self reference type.
  using super_type = ComboComparison; ///< Parent type.
public:
  static constexpr TextView KEY = "none-of"; ///< Comparison name.

  TextView const &
  key() const override
  {
    return KEY;
  }

  using Comparison::operator(); // declare hidden member function
  bool operator()(Context &ctx, Feature const &feature) const override;

  static Rv<Handle> load(Config &cfg, YAML::Node const &cmp_node, TextView const &key, TextView const &arg, YAML::Node value_node);

protected:
  Cmp_none_of(std::vector<Handle> &&cmps) : super_type(std::move(cmps)) {}
};

bool
Cmp_none_of::operator()(Context &ctx, Feature const &feature) const
{
  return std::none_of(_cmps.begin(), _cmps.end(), [&](Handle const &cmp) -> bool { return (*cmp)(ctx, feature); });
}

auto
Cmp_none_of::load(Config &cfg, YAML::Node const &cmp_node, TextView const &key, TextView const &arg, YAML::Node value_node)
  -> Rv<Handle>
{
  auto &&[cmps, errata] = super_type::load(cfg, cmp_node, key, arg, value_node);
  if (!errata.is_ok()) {
    return std::move(errata);
  }
  return Handle{new self_type{std::move(cmps)}};
}

// ---

class Cmp_for_all : public Comparison
{
  using self_type  = Cmp_for_all; ///< Self reference type.
  using super_type = Comparison;  ///< Parent type.
public:
  static constexpr TextView KEY = "for-all"; ///< Comparison name.
  static const ActiveType TYPES;             ///< Supported types.

  TextView const &
  key() const
  {
    return KEY;
  }

  using Comparison::operator(); // declare hidden member function
  bool operator()(Context &ctx, Feature const &feature) const override;

  static Rv<Handle> load(Config &cfg, YAML::Node const &cmp_node, TextView const &key, TextView const &arg, YAML::Node value_node);

protected:
  Comparison::Handle _cmp;
  explicit Cmp_for_all(Handle &&cmp) : _cmp(std::move(cmp)) {}
};

const ActiveType Cmp_for_all::TYPES{ActiveType::any_type()};

bool
Cmp_for_all::operator()(Context &ctx, Feature const &feature) const
{
  if (TUPLE != feature.value_type()) {
    return (*_cmp)(ctx, feature);
  }

  auto &t{std::get<TUPLE>(feature)};
  return std::all_of(t.begin(), t.end(), [&](Feature const &f) { return (*_cmp)(ctx, f); });
}

auto
Cmp_for_all::load(Config &cfg, YAML::Node const &cmp_node, TextView const &key, TextView const &, YAML::Node value_node)
  -> Rv<Handle>
{
  if (!value_node.IsMap()) {
    return Errata(S_ERROR, "{} comparison value at {} must be a single comparison.", key, value_node.Mark());
  }
  auto scope{cfg.feature_scope(cfg.active_type().tuple_types())};
  auto &&[cmp, errata] = Comparison::load(cfg, value_node);
  if (!errata.is_ok()) {
    errata.note("While parsing nested comparison of {} at {}.", key, cmp_node.Mark());
    return std::move(errata);
  }
  return Handle(new self_type{std::move(cmp)});
}

// ---

class Cmp_for_any : public Comparison
{
  using self_type  = Cmp_for_any; ///< Self reference type.
  using super_type = Comparison;  ///< Parent type.
public:
  static constexpr TextView KEY = "for-any"; ///< Comparison name.
  static const ActiveType TYPES;             ///< Supported types.

  TextView const &
  key() const
  {
    return KEY;
  }

  using Comparison::operator(); // declare hidden member function
  bool operator()(Context &ctx, Feature const &feature) const override;

  static Rv<Handle> load(Config &cfg, YAML::Node const &cmp_node, TextView const &key, TextView const &arg, YAML::Node value_node);

protected:
  Comparison::Handle _cmp;
  explicit Cmp_for_any(Handle &&cmp) : _cmp(std::move(cmp)) {}
};

const ActiveType Cmp_for_any::TYPES{ActiveType::any_type()};

bool
Cmp_for_any::operator()(Context &ctx, Feature const &feature) const
{
  if (TUPLE != feature.value_type()) {
    return (*_cmp)(ctx, feature);
  }

  auto &t{std::get<TUPLE>(feature)};
  return std::any_of(t.begin(), t.end(), [&](Feature const &f) { return (*_cmp)(ctx, f); });
}

auto
Cmp_for_any::load(Config &cfg, YAML::Node const &cmp_node, TextView const &key, TextView const &, YAML::Node value_node)
  -> Rv<Handle>
{
  if (!value_node.IsMap()) {
    return Errata(S_ERROR, "{} comparison value at {} must be a single comparison.", key, value_node.Mark());
  }
  auto scope{cfg.feature_scope(cfg.active_type().tuple_types())};
  auto &&[cmp, errata] = Comparison::load(cfg, value_node);
  if (!errata.is_ok()) {
    errata.note("While parsing nested comparison of {} at {}.", key, cmp_node.Mark());
    return std::move(errata);
  }
  return Handle(new self_type{std::move(cmp)});
}

// ---

class Cmp_for_none : public Comparison
{
  using self_type  = Cmp_for_none; ///< Self reference type.
  using super_type = Comparison;   ///< Parent type.
public:
  static constexpr TextView KEY = "for-none"; ///< Comparison name.
  static const ActiveType TYPES;              ///< Supported types.

  TextView const &
  key() const
  {
    return KEY;
  }

  using Comparison::operator(); // declare hidden member function
  bool operator()(Context &ctx, Feature const &feature) const override;

  static Rv<Handle> load(Config &cfg, YAML::Node const &cmp_node, TextView const &key, TextView const &arg, YAML::Node value_node);

protected:
  Comparison::Handle _cmp;
  explicit Cmp_for_none(Handle &&cmp) : _cmp(std::move(cmp)) {}
};

const ActiveType Cmp_for_none::TYPES{ActiveType::any_type()};

bool
Cmp_for_none::operator()(Context &ctx, Feature const &feature) const
{
  if (TUPLE != feature.value_type()) {
    return (*_cmp)(ctx, feature);
  }

  auto &t{std::get<TUPLE>(feature)};
  return std::all_of(t.begin(), t.end(), [&](Feature const &f) { return (*_cmp)(ctx, f); });
}

auto
Cmp_for_none::load(Config &cfg, YAML::Node const &cmp_node, TextView const &key, TextView const &, YAML::Node value_node)
  -> Rv<Handle>
{
  if (!value_node.IsMap()) {
    return Errata(S_ERROR, "{} comparison value at {} must be a single comparison.", key, value_node.Mark());
  }
  auto scope{cfg.feature_scope(cfg.active_type().tuple_types())};
  auto &&[cmp, errata] = Comparison::load(cfg, value_node);
  if (!errata.is_ok()) {
    errata.note("While parsing nested comparison of {} at {}.", key, cmp_node.Mark());
    return std::move(errata);
  }
  return Handle(new self_type{std::move(cmp)});
}

// ---

/// Compare the active feature as a tuple to a list of comparisons.
class Cmp_as_tuple : public ComboComparison
{
  using self_type  = Cmp_as_tuple;    ///< Self reference type.
  using super_type = ComboComparison; ///< Parent type.
public:
  static constexpr TextView KEY = "as-tuple"; ///< Comparison name.

  TextView const &
  key() const override
  {
    return KEY;
  }

  using Comparison::operator(); // declare hidden member function
  bool operator()(Context &ctx, Feature const &feature) const override;

  static Rv<Handle> load(Config &cfg, YAML::Node const &cmp_node, TextView const &key, TextView const &arg, YAML::Node value_node);

protected:
  explicit Cmp_as_tuple(std::vector<Handle> &&cmps) : super_type(std::move(cmps)) {}
};

bool
Cmp_as_tuple::operator()(Context &ctx, Feature const &feature) const
{
  if (_cmps.empty()) {
    return true;
  }

  auto vtype = feature.value_type();
  if (TUPLE != vtype) {
    return (*_cmps[0])(ctx, feature);
  }

  auto t      = std::get<TUPLE>(feature);
  auto t_size = t.size();
  auto c_size = _cmps.size();
  auto limit  = std::min(t_size, c_size);
  size_t idx  = 0;
  while (idx < limit) {
    if (!(*_cmps[idx])(ctx, t[idx])) {
      return false;
    }
    ++idx;
  }
  return true;
}

auto
Cmp_as_tuple::load(Config &cfg, YAML::Node const &cmp_node, TextView const &key, TextView const &arg, YAML::Node value_node)
  -> Rv<Handle>
{
  auto ttype = cfg.active_type().tuple_types();
  ActiveType atype{ttype};
  auto scope            = cfg.feature_scope(ttype); // drop tuple, use nested type.
  auto &&[cmps, errata] = super_type::load(cfg, cmp_node, key, arg, value_node);
  if (!errata.is_ok()) {
    return std::move(errata);
  }
  return Handle{new self_type{std::move(cmps)}};
}

// --- ComparisonGroup --- //

Errata
ComparisonGroupBase::load(Config &cfg, YAML::Node node)
{
  if (node.IsMap()) {
    auto errata{this->load_case(cfg, node)};
    if (!errata.is_ok()) {
      return errata;
    }
  } else if (node.IsSequence()) {
    for (auto child : node) {
      if (auto errata{this->load_case(cfg, child)}; !errata.is_ok()) {
        return errata;
      }
    }
  } else {
    return Errata(S_ERROR, "The node at {} was not comparison nor a list of comparisons as required.", node.Mark());
  }
  return {};
}

Rv<Comparison::Handle>
ComparisonGroupBase::load_cmp(Config &cfg, YAML::Node node)
{
  auto &&[handle, errata]{Comparison::load(cfg, node)};
  if (!errata.is_ok()) {
    return std::move(errata);
  }
  return std::move(handle);
}

// --- Initialization --- //

namespace
{
[[maybe_unused]] bool INITIALIZED = []() -> bool {
  Comparison::define(Cmp_otherwise::KEY, Cmp_otherwise::TYPES, Cmp_otherwise::load);
  Comparison::define(Cmp_is_true::KEY, Cmp_is_true::TYPES, Cmp_is_true::load);
  Comparison::define(Cmp_is_false::KEY, Cmp_is_false::TYPES, Cmp_is_false::load);
  Comparison::define(Cmp_is_null::KEY, Cmp_is_null::TYPES, Cmp_is_null::load);
  Comparison::define(Cmp_is_empty::KEY, Cmp_is_empty::TYPES, Cmp_is_empty::load);

  Comparison::define(Cmp_LiteralString::MATCH_KEY, Cmp_LiteralString::TYPES, Cmp_LiteralString::load);
  Comparison::define(Cmp_LiteralString::PREFIX_KEY, Cmp_LiteralString::TYPES, Cmp_LiteralString::load);
  Comparison::define(Cmp_LiteralString::SUFFIX_KEY, Cmp_LiteralString::TYPES, Cmp_LiteralString::load);
  Comparison::define(Cmp_LiteralString::CONTAIN_KEY, Cmp_LiteralString::TYPES, Cmp_LiteralString::load);
  Comparison::define(Cmp_LiteralString::TLD_KEY, Cmp_LiteralString::TYPES, Cmp_LiteralString::load);
  Comparison::define(Cmp_LiteralString::PATH_KEY, Cmp_LiteralString::TYPES, Cmp_LiteralString::load);

  Comparison::define(Cmp_Rxp::KEY, Cmp_Rxp::TYPES, Cmp_Rxp::load);

  Comparison::define(Cmp_eq::KEY, Cmp_eq::TYPES, Cmp_eq::load);
  Comparison::define(Cmp_ne::KEY, Cmp_ne::TYPES, Cmp_ne::load);
  Comparison::define(Cmp_lt::KEY, Cmp_le::TYPES, Cmp_lt::load);
  Comparison::define(Cmp_le::KEY, Cmp_lt::TYPES, Cmp_le::load);
  Comparison::define(Cmp_gt::KEY, Cmp_gt::TYPES, Cmp_gt::load);
  Comparison::define(Cmp_ge::KEY, Cmp_ge::TYPES, Cmp_ge::load);

  Comparison::define(Cmp_in::KEY, Cmp_in::TYPES, Cmp_in::load);

  Comparison::define(Cmp_none_of::KEY, Cmp_none_of::TYPES, Cmp_none_of::load);
  Comparison::define(Cmp_all_of::KEY, Cmp_all_of::TYPES, Cmp_all_of::load);
  Comparison::define(Cmp_any_of::KEY, Cmp_any_of::TYPES, Cmp_any_of::load);
  Comparison::define(Cmp_as_tuple::KEY, Cmp_as_tuple::TYPES, Cmp_as_tuple::load);

  Comparison::define(Cmp_for_all::KEY, Cmp_for_all::TYPES, Cmp_for_all::load);
  Comparison::define(Cmp_for_any::KEY, Cmp_for_any::TYPES, Cmp_for_any::load);
  Comparison::define(Cmp_for_none::KEY, Cmp_for_none::TYPES, Cmp_for_none::load);

  return true;
}();

} // namespace
