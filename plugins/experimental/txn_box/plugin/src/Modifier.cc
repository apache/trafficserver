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

#include "txn_box/Modifier.h"
#include "txn_box/Context.h"
#include "txn_box/Config.h"
#include "txn_box/Comparison.h"
#include "txn_box/yaml_util.h"

using swoc::TextView;
using swoc::Errata;
using swoc::Rv;
using namespace swoc::literals;

Errata
Modifier::define(swoc::TextView name, Modifier::Worker const &f)
{
  if (auto spot = _factory.find(name); spot == _factory.end()) {
    _factory.insert(spot, {name, f});
    return {};
  }
  return Errata(S_ERROR, R"(Modifier "{}" is already defined.)", name);
}

Rv<Modifier::Handle>
Modifier::load(Config &cfg, YAML::Node const &node, ActiveType ex_type)
{
  if (!node.IsMap()) {
    return Errata(S_ERROR, R"(Modifier at {} is not an object as required.)", node.Mark());
  }

  for (auto const &[key_node, value_node] : node) {
    TextView key{key_node.Scalar()};
    auto &&[arg, arg_errata]{parse_arg(key)};
    if (!arg_errata.is_ok()) {
      return std::move(arg_errata);
    }
    // See if @a key is in the factory.
    if (auto spot{_factory.find(key)}; spot != _factory.end()) {
      auto &&[handle, errata]{spot->second(cfg, node, key, arg, value_node)};

      if (!errata.is_ok()) {
        return std::move(errata);
      }
      if (!handle->is_valid_for(ex_type)) {
        return Errata(S_ERROR, R"(Modifier "{}" at {} cannot accept a feature of type "{}".)", key, node.Mark(), ex_type);
      }

      return std::move(handle);
    }
  }
  return Errata(S_ERROR, R"(No valid modifier key in object at {}.)", node.Mark());
}

swoc::Rv<Feature>
Modifier::operator()(Context &, feature_type_for<NIL>)
{
  return NIL_FEATURE;
}

swoc::Rv<Feature>
Modifier::operator()(Context &, feature_type_for<IP_ADDR>)
{
  return NIL_FEATURE;
}

swoc::Rv<Feature>
Modifier::operator()(Context &, feature_type_for<STRING>)
{
  return NIL_FEATURE;
}

// ---

class Mod_hash : public Modifier
{
  using self_type  = Mod_hash;
  using super_type = Modifier;

public:
  static const std::string KEY; ///< Identifier name.
  using Modifier::operator();   // declare hidden member function
  /** Modify the feature.
   *
   * @param ctx Run time context.
   * @param feature Feature to modify.
   * @return Errors, if any.
   */
  Rv<Feature> operator()(Context &ctx, feature_type_for<STRING> feature) override;

  /** Check if @a ftype is a valid type to be modified.
   *
   * @param ex_type Type of feature to modify.
   * @return @c true if this modifier can modity that feature type, @c false if not.
   */
  bool is_valid_for(ActiveType const &ex_type) const override;

  /// Resulting type of feature after modifying.
  ActiveType result_type(ActiveType const &) const override;

  /** Create an instance from YAML config.
   *
   * @param cfg Configuration state object.
   * @param mod_node Node with modifier.
   * @param key_node Node in @a mod_node that identifies the modifier.
   * @return A constructed instance or errors.
   */
  static Rv<Handle> load(Config &cfg, YAML::Node node, TextView key, TextView arg, YAML::Node key_value);

protected:
  unsigned _n = 0; ///< Number of hash buckets.

  /// Constructor for @c load.
  Mod_hash(unsigned n);
};

const std::string Mod_hash::KEY{"hash"};

Mod_hash::Mod_hash(unsigned n) : _n(n) {}

bool
Mod_hash::is_valid_for(ActiveType const &ex_type) const
{
  return ex_type.can_satisfy(STRING);
}

ActiveType
Mod_hash::result_type(ActiveType const &) const
{
  return {NIL, INTEGER};
}

Rv<Feature>
Mod_hash::operator()(Context &, feature_type_for<STRING> feature)
{
  feature_type_for<INTEGER> value = std::hash<std::string_view>{}(feature);
  return Feature{feature_type_for<INTEGER>{value % _n}};
}

Rv<Modifier::Handle>
Mod_hash::load(Config &, YAML::Node node, TextView, TextView, YAML::Node key_value)
{
  if (!key_value.IsScalar()) {
    return Errata(S_ERROR, R"(Value for "{}" at {} in modifier at {} is not a number as required.)", KEY, key_value.Mark(),
                  node.Mark());
  }
  TextView src{key_value.Scalar()}, parsed;
  src.trim_if(&isspace);
  auto n = swoc::svtou(src, &parsed);
  if (src.size() != parsed.size()) {
    return Errata(S_ERROR, R"(Value "{}" for "{}" at {} in modifier at {} is not a number as required.)", src, KEY,
                  key_value.Mark(), node.Mark());
  }
  if (n < 2) {
    return Errata(S_ERROR, R"(Value "{}" for "{}" at {} in modifier at {} must be at least 2.)", src, KEY, key_value.Mark(),
                  node.Mark());
  }

  return {Handle{new self_type(n)}, {}};
}

// ---

/// Do replacement based on regular expression matching.
class Mod_rxp_replace : public Modifier
{
  using self_type  = Mod_rxp_replace;
  using super_type = Modifier;

public:
  static inline const std::string KEY{"rxp-replace"}; ///< Identifier name.
  using Modifier::operator();                         // declare hidden member function

  static Rv<Handle> load(Config &cfg, YAML::Node node, TextView key, TextView arg, YAML::Node key_value);

  bool is_valid_for(ActiveType const &ex_type) const override;

  ActiveType result_type(ActiveType const &) const override;

  Rv<Feature> operator()(Context &ctx, feature_type_for<STRING> src) override;

  static constexpr TextView ARG_GLOBAL = "g";  ///< Global replace.
  static constexpr TextView ARG_NOCASE = "nc"; ///< Case insensitive match.
protected:
  RxpOp _op;         ///< Regular expression operator.
  Expr _replacement; ///< Replacement text.
  bool _global_p = false;

  Mod_rxp_replace(RxpOp &&op, Expr &&replacement) : _op(std::move(op)), _replacement(std::move(replacement)) {}
};

bool
Mod_rxp_replace::is_valid_for(ActiveType const &ex_type) const
{
  return ex_type.can_satisfy(STRING);
}

ActiveType
Mod_rxp_replace::result_type(ActiveType const &) const
{
  return {NIL, STRING};
}

Rv<Modifier::Handle>
Mod_rxp_replace::load(Config &cfg, YAML::Node node, TextView, TextView args, YAML::Node key_value)
{
  Rxp::Options opt;
  bool global_p = false;

  if (!key_value.IsSequence() || key_value.size() != 2) {
    return Errata(S_ERROR, R"(Value for modifier "{}" at {} is not list of size 2 - [ pattern, replacement ] - as required.)", KEY,
                  node.Mark());
  }

  while (args) {
    auto token = args.take_prefix_at(',');
    if (ARG_GLOBAL == token) {
      global_p = true;
    } else if (ARG_NOCASE == token) {
      opt.f.nc = true;
    } else {
      return Errata(S_ERROR, R"(Invalid option "{}" for modifier "{}" at {}.)", token, KEY, key_value.Mark());
    }
  }

  YAML::Node rxp_src_node = key_value[0];
  auto &&[pattern, pattern_errata]{cfg.parse_expr(key_value[0])};
  if (!pattern_errata.is_ok()) {
    pattern_errata.note(R"(While parsing expression for "{}" modifier at {}.)", KEY, key_value.Mark());
    return std::move(pattern_errata);
  }

  auto &&[op, op_errata]{RxpOp::load(cfg, std::move(pattern), opt)};
  if (!op_errata.is_ok()) {
    op_errata.note(R"(While parsing pattern for modifier "{}".)", KEY);
    return std::move(op_errata);
  }
  cfg.require_rxp_group_count(op.capture_count());

  auto &&[rep, rep_errata]{cfg.parse_expr(key_value[1])};
  if (!rep_errata.is_ok()) {
    rep_errata.note(R"(While parsing replacement for modifier "{}".)", KEY);
    return std::move(rep_errata);
  }

  auto self       = new self_type(std::move(op), std::move(rep));
  self->_global_p = global_p;
  return {Handle(self)};
}

Rv<Feature>
Mod_rxp_replace::operator()(Context &ctx, feature_type_for<STRING> src)
{
  /* Some unfortunate issues make this more complex than one might assume.
   * The root issue is the non-recursive nature of the transient buffer. To generate the
   * replacement text the feature for it must be extracted every time, because it is likely to
   * depend on extracting capture groups which can vary with every replacement. This will trash
   * the overall result if it also stored in the transient buffer. Therefore it is necessary to
   * construct pieces of the final string as the replacements are done, and then assemble them
   * later. At some point it could be desirable to make this sort of piece wise string a general
   * type that can be passed around, without having to immediately render it as a contiguous string.
   * For now, this is just here.
   */
  struct piece {
    TextView _text;
    piece *_next = nullptr;
  };

  piece anchor;          // to avoid checking for nullptr / start of list.
  piece *last = &anchor; // last piece added.
  [[maybe_unused]] int result;

  while (src && (result = _op(ctx, src)) > 0) {
    auto match = ctx.active_group(0);
    auto p     = ctx.make<piece>();
    p->_text.assign(src.data(), match.data());
    last->_next = p;
    last        = p;
    // write the replacement text.
    auto r = ctx.extract(_replacement);
    if (r.index() == IndexFor(STRING)) {
      ctx.commit(r);
      auto rp     = ctx.make<piece>();
      rp->_text   = std::get<IndexFor(STRING)>(r);
      last->_next = rp;
      last        = rp;
    }
    // Clip the match.
    src.assign(match.data_end(), src.data_end());
    if (!_global_p) {
      break;
    }
  }

  // How big is the result?
  size_t n = src.size();
  for (piece *spot = anchor._next; spot != nullptr; spot = spot->_next) {
    n += spot->_text.size();
  }

  // Pieces assemble!
  auto span = ctx.transient_buffer(n).rebind<char>();
  swoc::FixedBufferWriter w(span);
  for (piece *spot = anchor._next; spot != nullptr; spot = spot->_next) {
    w.write(spot->_text);
  }
  w.write(src);
  ctx.transient_finalize(w.size());

  return {FeatureView(w.view())};
}

// ---
/// Filter a list.
class Mod_filter : public FilterMod
{
  using self_type  = Mod_filter;
  using super_type = Modifier;

public:
  static inline const std::string KEY = "filter"; ///< Identifier name.
  using super_type::operator();                   // declare hidden member function
  /** Modify the feature.
   *
   * @param ctx Run time context.
   * @param feature Feature to modify [in,out]
   * @return Errors, if any.
   */
  Rv<Feature> operator()(Context &ctx, Feature &feature) override;

  /** Check if @a ftype is a valid type to be modified.
   *
   * @param ftype Type of feature to modify.
   * @return @c true if this modifier can modify that feature type, @c false if not.
   */
  bool is_valid_for(ActiveType const &ex_type) const override;

  /// Resulting type of feature after modifying.
  ActiveType result_type(ActiveType const &ex_type) const override;

  /** Create an instance from YAML config.
   *
   * @param cfg Configuration state object.
   * @param mod_node Node with modifier.
   * @param key_node Node in @a mod_node that identifies the modifier.
   * @return A constructed instance or errors.
   */
  static Rv<Handle> load(Config &cfg, YAML::Node node, TextView key, TextView arg, YAML::Node key_value);

  /// A filter comparison case.
  struct Case {
    Action _action = PASS;   ///< Action on match.
    Expr _expr;              ///< Replacement expression, if any.
    Comparison::Handle _cmp; ///< Comparison.

    /// Assign the comparison for this case.
    void assign(Comparison::Handle &&handle);

    bool operator()(Context &ctx, Feature const &feature);

    /** YAML load hook.
     *
     * @param cfg Configuration.
     * @param node Node containing comparison.
     * @return Errors, if any.
     *
     * This is called during case loading, before the generic loading is done. It is required to
     * check any non-generic keys and remove them - anything not expected by the generic load will
     * be flagged as an error.
     */
    Errata pre_load(Config &cfg, YAML::Node node);
  };

  /// Container for cases with comparisons.
  ComparisonGroup<Case> _cases;

  /** Perform the comparisons for the filter.
   *
   * @param ctx Txn context
   * @param feature Feature to compare
   * @return The matched @c Case or @c nullptr if no case matched.
   */
  Case const *compare(Context &ctx, Feature const &feature) const;
};

bool
Mod_filter::is_valid_for(ActiveType const &) const
{
  return true;
}

ActiveType
Mod_filter::result_type(ActiveType const &ex_type) const
{
  return ex_type;
}

auto
Mod_filter::compare(Context &ctx, Feature const &feature) const -> Case const *
{
  for (auto const &c : _cases) {
    if (!c._cmp || (*c._cmp)(ctx, feature)) {
      return &c;
    }
  }
  return nullptr;
}

Rv<Feature>
Mod_filter::operator()(Context &ctx, Feature &feature)
{
  Feature zret;
  if (feature.is_list()) {
    auto src    = std::get<IndexFor(TUPLE)>(feature);
    auto farray = static_cast<Feature *>(alloca(sizeof(Feature) * src.count()));
    feature_type_for<TUPLE> dst{farray, src.count()};
    unsigned dst_idx = 0;
    for (Feature f = feature; !is_nil(f); f = cdr(f)) {
      Feature item  = car(f);
      auto c        = _cases(ctx, item);
      Action action = ((c != _cases.end()) ? c->_action : DROP);
      switch (action) {
      case DROP:
        break;
      case PASS:
        dst[dst_idx++] = item;
        break;
      case REPLACE:
        dst[dst_idx++] = ctx.extract(c->_expr);
        break;
      }
    }
    auto span = ctx.alloc_span<Feature>(dst_idx);
    for (unsigned idx = 0; idx < dst_idx; ++idx) {
      span[idx] = dst[idx];
    }
    zret = span;
  } else {
    auto c        = this->compare(ctx, feature);
    Action action = c ? c->_action : DROP;
    switch (action) {
    case DROP:
      zret = NIL_FEATURE;
      break;
    case PASS:
      zret = feature;
      break;
    case REPLACE:
      zret = ctx.extract(c->_expr);
      break;
    }
  }
  return zret;
}

void
Mod_filter::Case::assign(Comparison::Handle &&handle)
{
  _cmp = std::move(handle);
}

Errata
Mod_filter::Case::pre_load(Config &cfg, YAML::Node cmp_node)
{
  if (!cmp_node.IsMap()) {
    return Errata(S_ERROR, "List element at {} for {} modifier is not a comparison object.", cmp_node.Mark(), KEY);
  }

  Expr replace_expr;
  unsigned action_count = 0;

  if (auto do_node = cmp_node[Global::DO_KEY]; do_node) {
    return Errata(S_ERROR, R"("{}" at line {} is not allowed in a modifier comparison.)", Global::DO_KEY, do_node.Mark());
  }

  YAML::Node drop_node = cmp_node[ACTION_DROP];
  if (drop_node) {
    _action = DROP;
    cmp_node.remove(ACTION_DROP);
    ++action_count;
  }

  YAML::Node pass_node = cmp_node[ACTION_PASS];
  if (pass_node) {
    _action = PASS;
    cmp_node.remove(ACTION_PASS);
    ++action_count;
  }

  YAML::Node replace_node = cmp_node[ACTION_REPLACE];
  if (replace_node) {
    auto &&[expr, errata] = cfg.parse_expr(replace_node);
    if (!errata.is_ok()) {
      errata.note("While parsing expression at {} for {} key in comparison at {}.", replace_node.Mark(), ACTION_REPLACE,
                  cmp_node.Mark());
      return std::move(errata);
    }
    _expr   = std::move(expr);
    _action = REPLACE;
    cmp_node.remove(ACTION_REPLACE);
    ++action_count;
  }

  if (action_count > 1) {
    return Errata(S_ERROR, "Only one of {}, {}, {} is allowed in the {} comparison at {}.", ACTION_REPLACE, ACTION_DROP,
                  ACTION_PASS, KEY, cmp_node.Mark());
  }

  return {};
}

bool
Mod_filter::Case::operator()(Context &ctx, Feature const &feature)
{
  return !_cmp || (*_cmp)(ctx, feature);
}

Rv<Modifier::Handle>
Mod_filter::load(Config &cfg, YAML::Node node, TextView, TextView, YAML::Node key_value)
{
  auto self = new self_type;
  Handle handle(self);
  auto active_type = cfg.active_type();
  auto scope{cfg.feature_scope(active_type.can_satisfy(TUPLE) ? active_type.tuple_types() : active_type)};

  if (auto errata = self->_cases.load(cfg, key_value); !errata.is_ok()) {
    errata.note(R"(While parsing modifier "{}" at line {}.)", KEY, node.Mark());
    return errata;
  }

  return handle;
}

// ---
/// Replace the feature with another feature if the input is nil or empty.
class Mod_else : public Modifier
{
  using self_type  = Mod_else;
  using super_type = Modifier;

public:
  static constexpr TextView KEY{"else"}; ///< Identifier name.
  using super_type::operator();          // declare hidden member function
  /** Modify the feature.
   *
   * @param ctx Run time context.
   * @param feature Feature to modify [in,out]
   * @return Errors, if any.
   */
  Rv<Feature> operator()(Context &ctx, Feature &feature) override;

  /** Check if @a ftype is a valid type to be modified.
   *
   * @param ftype Type of feature to modify.
   * @return @c true if this modifier can modity that feature type, @c false if not.
   */
  bool is_valid_for(ActiveType const &ex_type) const override;

  /// Resulting type of feature after modifying.
  ActiveType result_type(ActiveType const &) const override;

  /** Create an instance from YAML config.
   *
   * @param cfg Configuration state object.
   * @param mod_node Node with modifier.
   * @param key_node Node in @a mod_node that identifies the modifier.
   * @return A constructed instance or errors.
   */
  static Rv<Handle> load(Config &cfg, YAML::Node node, TextView key, TextView arg, YAML::Node key_value);

protected:
  Expr _value;

  explicit Mod_else(Expr &&fmt) : _value(std::move(fmt)) {}
};

bool
Mod_else::is_valid_for(ActiveType const &ex_type) const
{
  return ex_type.can_satisfy(MaskFor(NIL, STRING));
}

ActiveType
Mod_else::result_type(ActiveType const &) const
{
  return _value.result_type();
}

Rv<Feature>
Mod_else::operator()(Context &ctx, Feature &feature)
{
  return is_empty(feature) ? ctx.extract(_value) : feature;
}

Rv<Modifier::Handle>
Mod_else::load(Config &cfg, YAML::Node, TextView, TextView, YAML::Node key_value)
{
  auto &&[fmt, errata]{cfg.parse_expr(key_value)};
  if (!errata.is_ok()) {
    errata.note(R"(While parsing "{}" modifier at {}.)", KEY, key_value.Mark());
    return std::move(errata);
  }
  return Handle(new self_type{std::move(fmt)});
};

// ---
/// Concatenate a tuple into a string.
class Mod_join : public Modifier
{
  using self_type  = Mod_join;
  using super_type = Modifier;

public:
  static constexpr TextView KEY{"join"}; ///< Identifier name.
  using super_type::operator();          // declare hidden member function
  /** Modify the feature.
   *
   * @param ctx Run time context.
   * @param feature Feature to modify [in,out]
   * @return Errors, if any.
   */
  Rv<Feature> operator()(Context &ctx, Feature &feature) override;

  /** Check if @a ftype is a valid type to be modified.
   *
   * @param ftype Type of feature to modify.
   * @return @c true if this modifier can modity that feature type, @c false if not.
   */
  bool is_valid_for(ActiveType const &ex_type) const override;

  /// Resulting type of feature after modifying.
  ActiveType result_type(ActiveType const &) const override;

  /** Create an instance from YAML config.
   *
   * @param cfg Configuration state object.
   * @param mod_node Node with modifier.
   * @param key_node Node in @a mod_node that identifies the modifier.
   * @return A constructed instance or errors.
   */
  static Rv<Handle> load(Config &cfg, YAML::Node node, TextView key, TextView arg, YAML::Node key_value);

protected:
  Expr _separator;

  explicit Mod_join(Expr &&fmt) : _separator(std::move(fmt)) {}
};

bool
Mod_join::is_valid_for(ActiveType const &ex_type) const
{
  return ex_type.can_satisfy(MaskFor(NIL, STRING, TUPLE));
}

ActiveType
Mod_join::result_type(ActiveType const &) const
{
  return STRING;
}

Rv<Feature>
Mod_join::operator()(Context &ctx, Feature &feature)
{
  // Get the separator - if that doesn't work, leave it empty.
  TextView sep;
  auto value = ctx.extract(_separator);
  if (auto ptr = std::get_if<IndexFor(STRING)>(&value); ptr != nullptr) {
    sep = *ptr;
  }

  return feature.join(ctx, sep);
}

Rv<Modifier::Handle>
Mod_join::load(Config &cfg, YAML::Node, TextView, TextView, YAML::Node key_value)
{
  auto &&[expr, errata]{cfg.parse_expr(key_value)};
  if (!errata.is_ok()) {
    errata.note(R"(While parsing "{}" modifier at {}.)", KEY, key_value.Mark());
    return std::move(errata);
  }

  if (!expr.result_type().can_satisfy(STRING)) {
    errata.note(R"("{}" modifier at {} requires a string argument.)", KEY, key_value.Mark());
    return std::move(errata);
  }
  return Handle(new self_type{std::move(expr)});
};

// ---
/// Concatenate a string to the active feature.
class Mod_concat : public Modifier
{
  using self_type  = Mod_concat;
  using super_type = Modifier;

public:
  static constexpr TextView KEY{"concat"}; ///< Identifier name.
  using super_type::operator();            // declare hidden member function

  /** Modify the feature.
   *
   * @param ctx Run time context.
   * @param feature Feature to modify [in,out]
   * @return Errors, if any.
   */
  Rv<Feature> operator()(Context &ctx, Feature &feature) override;

  /** Check if @a ftype is a valid type to be modified.
   *
   * @param ftype Type of feature to modify.
   * @return @c true if this modifier can modity that feature type, @c false if not.
   */
  bool is_valid_for(ActiveType const &ex_type) const override;

  /// Resulting type of feature after modifying.
  ActiveType result_type(ActiveType const &) const override;

  /** Create an instance from YAML config.
   *
   * @param cfg Configuration state object.
   * @param mod_node Node with modifier.
   * @param key_node Node in @a mod_node that identifies the modifier.
   * @return A constructed instance or errors.
   */
  static Rv<Handle> load(Config &cfg, YAML::Node node, TextView key, TextView arg, YAML::Node key_value);

protected:
  Expr _expr;

  explicit Mod_concat(Expr &&expr) : _expr(std::move(expr)) {}

  struct Visitor {
    Context &_ctx;
    /// Active feature to modify.
    /// This will always be a @c STRING - if not, the visitor isn't called at all.
    Feature &_target;
    Visitor(Context &ctx, Feature &f) : _ctx(ctx), _target(f) {}

    Rv<Feature> operator()(feature_type_for<STRING> const &s);
    Rv<Feature> operator()(feature_type_for<TUPLE> const &t);

    /// @return An empty string if it is not a handled type.
    template <typename F>
    auto
    operator()(F const &) -> EnableForFeatureTypes<F, Rv<Feature>>
    {
      return _target;
    }
  };
};

bool
Mod_concat::is_valid_for(ActiveType const &ex_type) const
{
  return ex_type.can_satisfy({NIL, STRING});
}

ActiveType
Mod_concat::result_type(ActiveType const &) const
{
  return STRING;
}

Rv<Feature>
Mod_concat::Visitor::operator()(const feature_type_for<STRING> &s)
{
  if (s.empty()) {
    return _target;
  }

  TextView src{std::get<IndexFor(STRING)>(_target)};
  _ctx.transient_require(src.size() + s.size());
  auto view = _ctx.render_transient([&](swoc::BufferWriter &w) { w.write(src).write(s); });
  return {_ctx.commit(view)};
}

Rv<Feature>
Mod_concat::Visitor::operator()(const feature_type_for<TUPLE> &t)
{
  TextView src{std::get<IndexFor(STRING)>(_target)};
  if (t[0].index() != IndexFor(STRING) || t[1].index() != IndexFor(STRING)) {
    return _target;
  }
  TextView text{std::get<IndexFor(STRING)>(t[1])};
  if (text.empty()) {
    return _target;
  }
  TextView sep{std::get<IndexFor(STRING)>(t[0])};

  _ctx.transient_require(src.size() + sep.size() + text.size());
  auto view = _ctx.render_transient([=](swoc::BufferWriter &w) {
    w.write(src);
    if (src.size() && !src.ends_with(sep)) {
      w.write(sep);
    }
    w.write(text);
  });
  return {_ctx.commit(view)};
}

Rv<Feature>
Mod_concat::operator()(Context &ctx, Feature &feature)
{
  switch (feature.index()) {
  case IndexFor(STRING):
    break;
  case IndexFor(NIL): // treat NIL as the empty string.
    feature = FeatureView::Literal("");
    break;
  default:
    return feature;
  }

  Feature f{ctx.extract(_expr)};
  return std::visit(Visitor(ctx, feature), f);
}

Rv<Modifier::Handle>
Mod_concat::load(Config &cfg, YAML::Node, TextView, TextView, YAML::Node key_value)
{
  auto &&[expr, errata]{cfg.parse_expr(key_value)};
  if (!errata.is_ok()) {
    errata.note(R"(While parsing "{}" modifier at {}.)", KEY, key_value.Mark());
    return std::move(errata);
  }
  if (!expr.result_type().can_satisfy(MaskFor(STRING, TUPLE))) {
    errata.note(R"("{}" modifier at {} requires a string or a list of two strings.)", KEY, key_value.Mark());
    return std::move(errata);
  }
  return Handle(new self_type{std::move(expr)});
}

// ---

/// Convert the feature to boolean.
class Mod_as_bool : public Modifier
{
  using self_type  = Mod_as_bool;
  using super_type = Modifier;

public:
  inline static const std::string KEY = "as-bool"; ///< Identifier name.
  using super_type::operator();                    // declare hidden member function

  /** Modify the feature.
   *
   * @param ctx Run time context.
   * @param feature Feature to modify [in,out]
   * @return Errors, if any.
   */
  Rv<Feature> operator()(Context &ctx, Feature &feature) override;

  /** Check if @a ftype is a valid type to be modified.
   *
   * @param ftype Type of feature to modify.
   * @return @c true if this modifier can modity that feature type, @c false if not.
   */
  bool is_valid_for(ActiveType const &ex_type) const override;

  /// Resulting type of feature after modifying.
  ActiveType result_type(ActiveType const &) const override;

  /** Create an instance from YAML config.
   *
   * @param cfg Configuration state object.
   * @param mod_node Node with modifier.
   * @param key_node Node in @a mod_node that identifies the modifier.
   * @return A constructed instance or errors.
   */
  static Rv<Handle> load(Config &cfg, YAML::Node node, TextView key, TextView arg, YAML::Node key_value);

protected:
  inline static const auto VALUE_TYPES = MaskFor({STRING, INTEGER, FLOAT, BOOLEAN, TUPLE, IP_ADDR, NIL});
  Expr _value; ///< Default value.

  explicit Mod_as_bool(Expr &&expr) : _value(std::move(expr)) {}
};

bool
Mod_as_bool::is_valid_for(ActiveType const &ex_type) const
{
  return ex_type.can_satisfy(VALUE_TYPES);
}

ActiveType
Mod_as_bool::result_type(ActiveType const &) const
{
  return {BOOLEAN};
}

Rv<Feature>
Mod_as_bool::operator()(Context &, Feature &feature)
{
  return {feature.as_bool()};
}

Rv<Modifier::Handle>
Mod_as_bool::load(Config &cfg, YAML::Node, TextView, TextView, YAML::Node key_value)
{
  auto &&[expr, errata]{cfg.parse_expr(key_value)};
  if (!errata.is_ok()) {
    errata.note(R"(While parsing "{}" modifier at {}.)", KEY, key_value.Mark());
    return std::move(errata);
  }
  if (!(expr.is_null() || expr.result_type().can_satisfy(VALUE_TYPES))) {
    return Errata(S_ERROR, "Value of {} modifier is not of type {}.", KEY, VALUE_TYPES);
  }
  return Handle(new self_type{std::move(expr)});
}

// --- //
/// Convert the feature to an Integer.
class Mod_as_integer : public Modifier
{
  using self_type  = Mod_as_integer;
  using super_type = Modifier;

public:
  static constexpr TextView KEY{"as-integer"}; ///< Identifier name.
  using super_type::operator();                // declare hidden member function

  /** Modify the feature.
   *
   * @param ctx Run time context.
   * @param feature Feature to modify [in,out]
   * @return Errors, if any.
   */
  Rv<Feature> operator()(Context &ctx, Feature &feature) override;

  /** Check if @a ftype is a valid type to be modified.
   *
   * @param ftype Type of feature to modify.
   * @return @c true if this modifier can modity that feature type, @c false if not.
   */
  bool is_valid_for(ActiveType const &ex_type) const override;

  /// Resulting type of feature after modifying.
  ActiveType result_type(ActiveType const &) const override;

  /** Create an instance from YAML config.
   *
   * @param cfg Configuration state object.
   * @param mod_node Node with modifier.
   * @param key_node Node in @a mod_node that identifies the modifier.
   * @return A constructed instance or errors.
   */
  static Rv<Handle> load(Config &cfg, YAML::Node node, TextView key, TextView arg, YAML::Node key_value);

protected:
  Expr _value; ///< Default value.

  explicit Mod_as_integer(Expr &&expr) : _value(std::move(expr)) {}
};

bool
Mod_as_integer::is_valid_for(ActiveType const &ex_type) const
{
  return ex_type.can_satisfy(MaskFor({STRING, INTEGER, FLOAT, BOOLEAN, NIL}));
}

ActiveType
Mod_as_integer::result_type(ActiveType const &) const
{
  return {MaskFor({NIL, INTEGER})};
}

Rv<Feature>
Mod_as_integer::operator()(Context &ctx, Feature &feature)
{
  auto &&[value, errata]{feature.as_integer()};
  if (errata.is_ok()) {
    return Feature{value};
  }
  auto invalid{ctx.extract(_value)};
  if (errata.is_ok()) {
    return Feature{invalid};
  }
  return feature;
}

Rv<Modifier::Handle>
Mod_as_integer::load(Config &cfg, YAML::Node, TextView, TextView, YAML::Node key_value)
{
  auto &&[expr, errata]{cfg.parse_expr(key_value)};
  if (!errata.is_ok()) {
    errata.note(R"(While parsing "{}" modifier at {}.)", KEY, key_value.Mark());
    return std::move(errata);
  }
  if (!(expr.is_null() || expr.result_type().can_satisfy(MaskFor(INTEGER)))) {
    return Errata(S_ERROR, "Value of {} modifier is not of type {}.", KEY, INTEGER);
  }
  return Handle(new self_type{std::move(expr)});
}

// --- //

/// Convert the feature to an IP address.
class Mod_as_ip_addr : public Modifier
{
  using self_type  = Mod_as_ip_addr;
  using super_type = Modifier;

public:
  static const std::string KEY; ///< Identifier name.
  using super_type::operator(); // declare hidden member function
  /** Modify the feature.
   *
   * @param ctx Run time context.
   * @param feature Feature to modify [in,out]
   * @return Errors, if any.
   */
  Rv<Feature> operator()(Context &ctx, Feature &feature) override;

  /** Check if @a ftype is a valid type to be modified.
   *
   * @param ftype Type of feature to modify.
   * @return @c true if this modifier can modity that feature type, @c false if not.
   */
  bool is_valid_for(ActiveType const &ex_type) const override;

  /// Resulting type of feature after modifying.
  ActiveType result_type(ActiveType const &) const override;

  /** Create an instance from YAML config.
   *
   * @param cfg Configuration state object.
   * @param mod_node Node with modifier.
   * @param key_node Node in @a mod_node that identifies the modifier.
   * @return A constructed instance or errors.
   */
  static Rv<Handle> load(Config &cfg, YAML::Node node, TextView key, TextView arg, YAML::Node key_value);

protected:
  explicit Mod_as_ip_addr() = default;

  /// Identity conversion.
  Feature convert(Context &ctx, feature_type_for<IP_ADDR> n);
  /// Convert from string
  Feature convert(Context &ctx, feature_type_for<STRING> s);

  /// Generic failure case.
  template <typename T>
  auto
  convert(Context &, T &) -> EnableForFeatureTypes<T, Feature>
  {
    return NIL_FEATURE;
  }
};

const std::string Mod_as_ip_addr::KEY{"as-ip-addr"};

bool
Mod_as_ip_addr::is_valid_for(ActiveType const &ex_type) const
{
  return ex_type.can_satisfy(MaskFor({IP_ADDR, STRING}));
}

ActiveType
Mod_as_ip_addr::result_type(ActiveType const &) const
{
  return {MaskFor({NIL, IP_ADDR})};
}

Rv<Feature>
Mod_as_ip_addr::operator()(Context &ctx, Feature &feature)
{
  auto visitor = [&](auto &t) { return this->convert(ctx, t); };
  return std::visit(visitor, feature);
}

auto
Mod_as_ip_addr::load(Config &, YAML::Node, TextView, TextView, YAML::Node) -> Rv<Handle>
{
  return Handle(new self_type);
}

Feature
Mod_as_ip_addr::convert(Context &, feature_type_for<IP_ADDR> n)
{
  return n;
}

Feature
Mod_as_ip_addr::convert(Context &, feature_type_for<STRING> s)
{
  swoc::IPAddr addr{s};
  return addr.is_valid() ? Feature{addr} : NIL_FEATURE;
};

// ---

/// Convert the feature to a Duration.
class Mod_As_Duration : public Modifier
{
  using self_type  = Mod_As_Duration;
  using super_type = Modifier;

public:
  static const std::string KEY; ///< Identifier name.
  using super_type::operator(); // declare hidden member function

  /** Modify the feature.
   *
   * @param ctx Run time context.
   * @param feature Feature to modify [in,out]
   * @return Errors, if any.
   */
  Rv<Feature> operator()(Context &ctx, Feature &feature) override;

  /** Check if @a ftype is a valid type to be modified.
   *
   * @param ftype Type of feature to modify.
   * @return @c true if this modifier can modity that feature type, @c false if not.
   */
  bool is_valid_for(ActiveType const &ex_type) const override;

  /// Resulting type of feature after modifying.
  ActiveType result_type(ActiveType const &) const override;

  /** Create an instance from YAML config.
   *
   * @param cfg Configuration state object.
   * @param mod_node Node with modifier.
   * @param key_node Node in @a mod_node that identifies the modifier.
   * @return A constructed instance or errors.
   */
  static Rv<Handle> load(Config &cfg, YAML::Node node, TextView key, TextView arg, YAML::Node key_value);

protected:
  Expr _value; ///< Default value.

  explicit Mod_As_Duration(Expr &&expr) : _value(std::move(expr)) {}
};

const std::string Mod_As_Duration::KEY{"as-duration"};

bool
Mod_As_Duration::is_valid_for(ActiveType const &ex_type) const
{
  return ex_type.can_satisfy(MaskFor({STRING, DURATION, TUPLE, NIL}));
}

ActiveType
Mod_As_Duration::result_type(ActiveType const &) const
{
  return {MaskFor({NIL, DURATION})};
}

Rv<Feature>
Mod_As_Duration::operator()(Context &, Feature &feature)
{
  auto &&[duration, errata]{feature.as_duration()};
  return {Feature(duration), std::move(errata)};
}

Rv<Modifier::Handle>
Mod_As_Duration::load(Config &cfg, YAML::Node, TextView, TextView, YAML::Node key_value)
{
  auto &&[expr, errata]{cfg.parse_expr(key_value)};
  if (!errata.is_ok()) {
    errata.note(R"(While parsing "{}" modifier at {}.)", KEY, key_value.Mark());
    return std::move(errata);
  }
  return Handle(new self_type{std::move(expr)});
}

// ---
/// url-encode modifier.
class Mod_url_encode : public Modifier
{
  using self_type  = Mod_url_encode;
  using super_type = Modifier;

  // Our own map. TSStringPercentEncode will not escape all we need.
  static constexpr unsigned char escape_codes[32] = {
    0xFF, 0xFF, 0xFF,
    0xFF,       // control
    0xBE,       // space ” # % $ &
    0x19,       // + , /
    0x00,       //
    0x3F,       // < > : ; = ?
    0x80,       // @
    0x00, 0x00, //
    0x1E, 0x80, // [ \ ] ^ `
    0x00, 0x00, //
    0x1F,       // { | } ~ DEL
    0x00, 0x00, 0x00,
    0x00, // all non-ascii characters unmodified
    0x00, 0x00, 0x00,
    0x00, //
    0x00, 0x00, 0x00,
    0x00, //
    0x00, 0x00, 0x00,
    0x00 //
  };

public:
  inline static const std::string KEY = "url-encode";
  using super_type::operator(); // declare hidden member function

  /** Check if @a ftype is a valid type to be modified.
   *
   * @param ftype Type of feature to modify.
   * @return @c true if this modifier can modity that feature type, @c false if not.
   */
  bool is_valid_for(ActiveType const &ex_type) const override;

  /// Resulting type of feature after modifying.
  ActiveType result_type(ActiveType const &) const override;

  /** Modify the feature.
   *
   * @param ctx Run time context.
   * @param feature Feature to modify
   * @return Errors, if any.
   */
  Rv<Feature> operator()(Context &ctx, feature_type_for<STRING> feature) override;

  /** Create an instance from YAML config.
   *
   * @param cfg Configuration state object.
   * @param mod_node Node with modifier.
   * @param key_node Node in @a mod_node that identifies the modifier.
   * @return A constructed instance or errors.
   */
  static Rv<super_type::Handle> load(Config &, YAML::Node, TextView, TextView, YAML::Node);
};

bool
Mod_url_encode::is_valid_for(ActiveType const &ex_type) const
{
  return ex_type.can_satisfy(MaskFor(NIL, STRING));
}

ActiveType
Mod_url_encode::result_type(ActiveType const &) const
{
  return {MaskFor({NIL, STRING})};
}

Rv<Modifier::Handle>
Mod_url_encode::load(Config &, YAML::Node, TextView, TextView, YAML::Node)
{
  return Modifier::Handle(new self_type);
}

Rv<Feature>
Mod_url_encode::operator()(Context &ctx, feature_type_for<STRING> feature)
{
  const size_t size = feature.size() * 3; // *3 should suffice.
  size_t length;
  auto buff = ctx.transient_buffer(size);
  if (TS_SUCCESS == TSStringPercentEncode(feature.data(), feature.size(), buff.data(), size, &length, escape_codes)) {
    ctx.transient_finalize(length).commit_transient();    // adjust the transient buffer length and commit it.
    return {FeatureView::Literal({buff.data(), length})}; // literal because it's committed.
  }
  return NIL_FEATURE;
}

// ---
/// url-decode modifier
class Mod_url_decode : public Modifier
{
  using self_type  = Mod_url_decode;
  using super_type = Modifier;

public:
  inline static const std::string KEY = "url-decode";
  using super_type::operator(); // declare hidden member function

  /** Check if @a ftype is a valid type to be modified.
   *
   * @param ftype Type of feature to modify.
   * @return @c true if this modifier can modity that feature type, @c false if not.
   */
  bool is_valid_for(ActiveType const &ex_type) const override;

  /// Resulting type of feature after modifying.
  ActiveType result_type(ActiveType const &) const override;

  /** Modify the feature.
   *
   * @param ctx Run time context.
   * @param feature Feature to modify
   * @return Errors, if any.
   *
   */
  Rv<Feature> operator()(Context &ctx, feature_type_for<STRING> feature) override;

  /** Create an instance from YAML config.
   *
   * @param cfg Configuration state object.
   * @param mod_node Node with modifier.
   * @param key_node Node in @a mod_node that identifies the modifier.
   * @return A constructed instance or errors.
   */
  static Rv<super_type::Handle> load(Config &, YAML::Node, TextView, TextView, YAML::Node);
};

bool
Mod_url_decode::is_valid_for(ActiveType const &ex_type) const
{
  return ex_type.can_satisfy(MaskFor(NIL, STRING));
}

ActiveType
Mod_url_decode::result_type(ActiveType const &) const
{
  return {MaskFor({NIL, STRING})};
}

Rv<Modifier::Handle>
Mod_url_decode::load(Config &, YAML::Node, TextView, TextView, YAML::Node)
{
  return Modifier::Handle(new self_type);
}

Rv<Feature>
Mod_url_decode::operator()(Context &ctx, feature_type_for<STRING> feature)
{
  const size_t size = feature.size();
  size_t length;
  auto buff = ctx.transient_buffer(size);
  if (TS_SUCCESS == TSStringPercentDecode(feature.data(), size, buff.data(), size, &length)) {
    ctx.transient_finalize(length).commit_transient();    // adjust the transient buffer length and commit it.
    return {FeatureView::Literal({buff.data(), length})}; // literal because it's committed.
  }
  return NIL_FEATURE;
}
// --- //

namespace
{
[[maybe_unused]] bool INITIALIZED = []() -> bool {
  Modifier::define(Mod_hash::KEY, &Mod_hash::load);
  Modifier::define(Mod_else::KEY, &Mod_else::load);
  Modifier::define(Mod_join::KEY, &Mod_join::load);
  Modifier::define(Mod_concat::KEY, &Mod_concat::load);
  Modifier::define(Mod_as_bool::KEY, &Mod_as_bool::load);
  Modifier::define(Mod_as_integer::KEY, &Mod_as_integer::load);
  Modifier::define(Mod_As_Duration::KEY, &Mod_As_Duration::load);
  Modifier::define(Mod_filter::KEY, &Mod_filter::load);
  Modifier::define(Mod_as_ip_addr::KEY, &Mod_as_ip_addr::load);
  Modifier::define(Mod_rxp_replace::KEY, &Mod_rxp_replace::load);
  Modifier::define(Mod_url_encode::KEY, &Mod_url_encode::load);
  Modifier::define(Mod_url_decode::KEY, &Mod_url_decode::load);
  return true;
}();
} // namespace
