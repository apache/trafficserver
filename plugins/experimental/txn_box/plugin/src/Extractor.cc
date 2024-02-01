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

#include <swoc/TextView.h>
#include <swoc/Errata.h>
#include <swoc/bwf_ip.h>

#include "txn_box/common.h"
#include "txn_box/Expr.h"
#include "txn_box/Extractor.h"
#include "txn_box/Context.h"
#include "txn_box/Config.h"

using swoc::TextView;
using swoc::MemSpan;
using swoc::Errata;
using swoc::Rv;
using swoc::BufferWriter;
namespace bwf = swoc::bwf;
using namespace swoc::literals;

/* ------------------------------------------------------------------------------------ */
static_assert(sizeof(Extractor::Spec::union_type) == sizeof(MemSpan<void>));
/* ------------------------------------------------------------------------------------ */
swoc::Lexicon<BoolTag> const BoolNames{
  {{BoolTag::True, {"true", "1", "on", "enable", "Y", "yes"}}, {BoolTag::False, {"false", "0", "off", "disable", "N", "no"}}},
  {BoolTag::INVALID}
};
/* ------------------------------------------------------------------------------------ */
Errata
Extractor::define(TextView name, self_type *ex)
{
  _ex_table[name] = ex;
  return {};
}

bool
Extractor::has_ctx_ref() const
{
  return false;
}

swoc::Rv<ActiveType>
Extractor::validate(Config &, Extractor::Spec &, TextView const &)
{
  return ActiveType{NIL, STRING};
}

Feature
Extractor::extract(Config &, Extractor::Spec const &)
{
  return NIL_FEATURE;
}

BufferWriter &
Extractor::format(BufferWriter &w, Spec const &spec, Context &ctx)
{
  return bwformat(w, spec, this->extract(ctx, spec));
}

Extractor::self_type *
Extractor::find(TextView const &name)
{
  auto spot{_ex_table.find(name)};
  return spot == _ex_table.end() ? nullptr : spot->second;
}

/* ---------------------------------------------------------------------------------------------- */
auto
FeatureGroup::Tracking::find(swoc::TextView const &name) const -> Tracking::Info *
{
  Info *spot = std::find_if(_info.begin(), _info.end(), [&](auto &t) { return 0 == strcasecmp(t._name, name); });
  return spot == _info.end() ? nullptr : spot;
}
auto
FeatureGroup::Tracking::obtain(swoc::TextView const &name) -> Tracking::Info *
{
  Info *spot = this->find(name);
  if (nullptr == spot) {
    spot        = this->alloc();
    spot->_name = name;
  }
  return spot;
}

FeatureGroup::index_type
FeatureGroup::index_of(swoc::TextView const &name)
{
  auto spot =
    std::find_if(_expr_info.begin(), _expr_info.end(), [&name](ExprInfo const &info) { return 0 == strcasecmp(info._name, name); });
  return spot == _expr_info.end() ? INVALID_IDX : spot - _expr_info.begin();
}

Errata
FeatureGroup::load_expr(Config &cfg, Tracking &tracking, Tracking::Info *info, YAML::Node const &node)
{
  /* A bit tricky, but not unduly so. The goal is to traverse all of the specifiers in the
   * expression and convert generic "this" extractors to the "this" extractor for @a this
   * feature group instance. This struct is required by the visitation rules of @c std::variant.
   * There's an overload for each variant type in @c Expr plus a common helper method.
   * It's not possible to use lambda gathering because the @c List case is recursive.
   */
  struct V {
    V(FeatureGroup &fg, Config &cfg, Tracking &tracking) : _fg(fg), _cfg(cfg), _tracking(tracking) {}
    FeatureGroup &_fg;
    Config &_cfg;
    Tracking &_tracking;
    bool _dependent_p = false;

    /// Update @a spec as needed to have the correct "this" extractor.
    Errata
    load_spec(Extractor::Spec &spec)
    {
      if (spec._exf == &ex_this) {
        auto &&[tinfo, errata] = _fg.load_key(_cfg, _tracking, spec._ext);
        if (errata.is_ok()) {
          spec._exf = &_fg._ex_this;
          // Don't track if the target is a literal - no runtime extraction needed.
          if (!tinfo->_expr.is_literal()) {
            _dependent_p = true;
            // If not already marked as a reference target mark it.
            if (tinfo->_exf_idx == INVALID_IDX) {
              tinfo->_exf_idx = _fg._ref_count++;
              // This marking happens after the depth first dependency chain has been explored
              // therefore all dependencies for this target are already in the @a _order_idx
              // elements, and therefore it is time to add this one.
              _tracking._info[tinfo->_exf_idx]._order_idx = tinfo - _tracking._info.data();
            }
            // Invariant - @a _dependent_p is true => @a _ref_count is non-zero.
          }
        }
        return std::move(errata);
      }
      return {};
    }

    Errata
    operator()(std::monostate)
    {
      return {};
    }
    Errata
    operator()(Feature const &)
    {
      return {};
    }
    Errata
    operator()(Expr::Direct &d)
    {
      return this->load_spec(d._spec);
    }
    Errata
    operator()(Expr::Composite &c)
    {
      for (auto &spec : c._specs) {
        auto errata = this->load_spec(spec);
        if (!errata.is_ok()) {
          return errata;
        }
      }
      return {};
    }
    // For list, it's a list of nested @c Expr instances, so visit those as this one was visited.
    Errata
    operator()(Expr::List &l)
    {
      for (auto &expr : l._exprs) {
        auto errata = std::visit(*this, expr._raw);
        if (!errata.is_ok()) {
          return errata;
        }
      }
      return {};
    }
  } v(*this, cfg, tracking);

  auto &&[expr, errata]{cfg.parse_expr(node)};
  info->_expr = std::move(expr);
  if (errata.is_ok()) {
    errata             = std::visit(v, info->_expr._raw); // update "this" extractor references.
    info->_dependent_p = v._dependent_p;
  }
  return std::move(errata);
}

auto
FeatureGroup::load_key(Config &cfg, FeatureGroup::Tracking &tracking, swoc::TextView name) -> Rv<Tracking::Info *>
{
  YAML::Node n{tracking._node[name]};

  // Check if the key is present in the node. If not, it must be a referenced key because
  // the presence of explicit keys is checked before loading any keys.
  if (!n) {
    return Errata(S_ERROR, R"("{}" is referenced but no such key was found.)", name);
  }

  auto tinfo = tracking.obtain(name);

  if (tinfo->_mark == DONE) { // already loaded, presumably due to a reference.
    return tinfo;
  }

  if (tinfo->_mark == IN_PLAY) {
    return Errata(S_ERROR, R"(Circular dependency for key "{}" at {}.)", name, tracking._node.Mark());
  }
  tinfo->_mark = IN_PLAY;

  Errata errata{this->load_expr(cfg, tracking, tinfo, n)};
  if (!errata.is_ok()) {
    errata.note(R"(While loading extraction format for key "{}" at {}.)", name, tracking._node.Mark());
    return errata;
  }

  tinfo->_mark = DONE;
  return tinfo;
}

Errata
FeatureGroup::load(Config &cfg, YAML::Node const &node, std::initializer_list<FeatureGroup::Descriptor> const &ex_keys)
{
  unsigned n_keys = node.size(); // Number of keys in @a node.

  Tracking::Info tracking_info[n_keys];
  Tracking tracking(node, tracking_info, n_keys);

  // Find the roots of extraction - these are the named keys actually in the node.
  // Need to do this explicitly to transfer the flags, and to check for duplicates in @a ex_keys.
  // It is not an error for a named key to be missing unless it's marked @c REQUIRED.
  for (auto &d : ex_keys) {
    auto tinfo = tracking.find(d._name);
    if (nullptr != tinfo) {
      return Errata(
        S_ERROR, R"("INTERNAL ERROR: "{}" is used more than once in the extractor key list of the feature group for the node {}.)",
        d._name, node.Mark());
    }
    if (node[d._name]) {
      tinfo        = tracking.alloc();
      tinfo->_name = d._name;
    } else if (d._flags[REQUIRED]) {
      return Errata(S_ERROR, R"(The required key "{}" was not found in the node {}.)", d._name, node.Mark());
    }
  }

  // Time to get the expressions and walk the references. Need to finalize the range before calling
  // @c load_key as that can modify @a tracking_count. Also must avoid calling this on keys that
  // are explicit but not required - need to fail on missing keys iff they're referenced, which is
  // checked by @c load_key. The presence of required keys has already been verified.
  for (auto info = tracking_info, limit = info + tracking._count; info < limit; ++info) {
    auto &&[dummy, errata]{this->load_key(cfg, tracking, info->_name)};
    if (!errata.is_ok()) {
      return std::move(errata);
    }
  }

  // Persist the tracking info, now that all the sizes are known.
  _expr_info = cfg.alloc_span<ExprInfo>(tracking._count);
  _expr_info.apply([](ExprInfo &info) { new (&info) ExprInfo; });

  // If there are dependencies, allocate state to hold cached values.
  // If any key was marked dependent, then @a _ref_count > 0.
  if (_ref_count > 0) {
    _ctx_state_span = cfg.reserve_ctx_storage(sizeof(State));
    _ordering       = cfg.alloc_span<index_type>(_ref_count);
    for (index_type idx = 0; idx < _ref_count; ++idx) {
      _ordering[idx] = tracking._info[idx]._order_idx;
    }
  }

  // Persist the keys by copying persistent data from the tracking data to config allocated space.
  for (unsigned short idx = 0; idx < tracking._count; ++idx) {
    Tracking::Info &src = tracking._info[idx];
    ExprInfo &dst       = _expr_info[idx];
    dst._name           = src._name;
    dst._expr           = std::move(src._expr);
    dst._exf_idx        = src._exf_idx;
    dst._dependent_p    = src._dependent_p;
  }

  return {};
}

Errata
FeatureGroup::load_as_scalar(Config &cfg, const YAML::Node &value, swoc::TextView const &name)
{
  auto &&[expr, errata]{cfg.parse_expr(value)};
  if (!errata.is_ok()) {
    return std::move(errata);
  }
  _expr_info = cfg.alloc_span<ExprInfo>(1);
  auto info  = _expr_info.data();
  new (info) ExprInfo;
  info->_expr = std::move(expr);
  info->_name = name;
  return {};
}

Errata
FeatureGroup::load_as_tuple(Config &cfg, YAML::Node const &node, std::initializer_list<FeatureGroup::Descriptor> const &ex_keys)
{
  unsigned idx    = 0;
  unsigned n_keys = ex_keys.size();
  unsigned n_elts = node.size();
  ExprInfo info[n_keys];

  // No dependency in tuples, can just walk the keys and load them.
  for (auto const &key : ex_keys) {
    if (idx >= n_elts) {
      if (key._flags[REQUIRED]) {
        return Errata(S_ERROR, R"(The list was {} elements long but {} are required.)", n_elts, n_keys);
      }
      continue; // it was optional, skip it and keep checking for REQUIRED keys.
    }

    auto &&[expr, errata] = cfg.parse_expr(node[idx]);
    if (!errata.is_ok()) {
      return std::move(errata);
    }
    info[idx]._name = key._name;
    info[idx]._expr = std::move(expr); // safe because of the @c reserve
    ++idx;
  }
  // Localize feature info, now that the size is determined.
  _expr_info   = cfg.alloc_span<ExprInfo>(idx);
  index_type i = 0;
  for (auto &item : _expr_info) {
    new (&item) ExprInfo{std::move(info[i++])};
  }
  // No dependencies for tuple loads.
  // No feature data because no dependencies.

  return {};
}

Feature
FeatureGroup::extract(Context &ctx, swoc::TextView const &name)
{
  auto idx = this->index_of(name);
  return idx == INVALID_IDX ? NIL_FEATURE : this->extract(ctx, idx);
}

Feature
FeatureGroup::extract(Context &ctx, index_type idx)
{
  auto &info = _expr_info[idx];
  if (info._dependent_p || info._exf_idx != INVALID_IDX) {
    // State is always allocated if there are any dependents. Need to fill the cache if this is
    // a key that's either dependent or one of the dependency targets.
    State &state = ctx.initialized_storage_for<State>(_ctx_state_span)[0];
    if (state._features.empty()) { // no target has yet been extracted, do it now.
      // Allocate target cache.
      state._features = ctx.alloc_span<Feature>(_ref_count);
      // Extract targets.
      for (index_type target_idx : _ordering) {
        auto &target{_expr_info[target_idx]};
        state._features[target._exf_idx] = ctx.extract(target._expr);
      }
    }
  }

  if (info._exf_idx != INVALID_IDX) { // it's a target so it's in the cache - fetch it.
    State &state = ctx.storage_for(_ctx_state_span).rebind<State>()[0];
    return state._features[info._exf_idx];
  }

  return ctx.extract(info._expr);
}

FeatureGroup::~FeatureGroup()
{
  _expr_info.apply([](ExprInfo &info) { std::destroy_at(&info); });
}

/* ---------------------------------------------------------------------------------------------- */
Feature
StringExtractor::extract(Context &ctx, Spec const &spec)
{
  return ctx.render_transient([&](BufferWriter &w) { this->format(w, spec, ctx); });
}
/* ------------------------------------------------------------------------------------ */
// Utilities.
bool
Feature::is_list() const
{
  auto idx = this->index();
  return IndexFor(TUPLE) == idx || IndexFor(CONS) == idx;
}
// ----
ActiveType
Feature::active_type() const
{
  auto vt       = this->value_type();
  ActiveType at = vt;
  if (TUPLE == vt) {
    auto &tp = std::get<IndexFor(TUPLE)>(*this);
    if (tp.size() == 0) { // empty tuple can be a tuple of any type.
      at = ActiveType::TupleOf(ActiveType::any_type().base_types());
    } else if (auto tt = tp[0].value_type();
               std::all_of(tp.begin() + 1, tp.end(), [=](Feature const &f) { return f.value_type() == tt; })) {
      at = ActiveType::TupleOf(tt);
    } // else leave it as just a tuple with no specific type.
  }
  return at;
}
// ----
Feature
car(Feature const &feature)
{
  switch (feature.index()) {
  case IndexFor(CONS):
    return std::get<IndexFor(CONS)>(feature)->_car;
  case IndexFor(TUPLE):
    return std::get<IndexFor(TUPLE)>(feature)[0];
  case IndexFor(GENERIC): {
    auto gf = std::get<IndexFor(GENERIC)>(feature);
    if (gf) {
      return gf->extract();
    }
  }
  }
  return feature;
}
// ----
Feature &
cdr(Feature &feature)
{
  switch (feature.index()) {
  case IndexFor(CONS):
    feature = std::get<feature_type_for<CONS>>(feature)->_cdr;
    break;
  case IndexFor(TUPLE): {
    Feature cdr{feature};
    auto &span = std::get<feature_type_for<TUPLE>>(cdr);
    span.remove_prefix(1);
    feature = span.empty() ? NIL_FEATURE : cdr;
  } break;
  }
  return feature;
}
/* ---------------------------------------------------------------------------------------------- */
