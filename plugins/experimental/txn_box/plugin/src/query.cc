/** @file
   Query string support.

 * Copyright 2021, Yahoo!
 * SPDX-License-Identifier: Apache-2.0
*/

#include <string>

#include <swoc/TextView.h>
#include <swoc/Errata.h>
#include <swoc/BufferWriter.h>
#include <swoc/bwf_ex.h>

#include "txn_box/common.h"
#include "txn_box/Config.h"
#include "txn_box/Context.h"
#include "txn_box/Extractor.h"
#include "txn_box/Directive.h"
#include "txn_box/Comparison.h"

#include "txn_box/ts_util.h"

using swoc::TextView;
using swoc::Errata;
using swoc::Rv;
using swoc::BufferWriter;
using namespace swoc::literals;

/* ------------------------------------------------------------------------------------ */
namespace
{
/// Standard caseless compare arg.
static inline constexpr TextView ARG_NOCASE = "nc";
/// Standard reverse order arg.
static inline constexpr TextView ARG_REVERSE = "rev";

struct QPair {
  using self_type = QPair;

  TextView name;
  TextView value;
  char elt_sep     = 0;   ///< Separator before name.
  char kv_sep      = '='; ///< Separator for name/value - always '=' if not null.
  self_type *_next = nullptr;
  self_type *_prev = nullptr;
  using Linkage    = swoc::IntrusiveLinkage<self_type, &self_type::_next, &self_type::_prev>;

  QPair() = default;
  QPair(TextView k, TextView v) : name(k), value(v) {}

  TextView
  all() const
  {
    return TextView(name.data(), value.data_end());
  }
};

QPair
query_take_qpair(TextView &qs)
{
  char elt_sep = 0;
  // Strip the leading separator, if any, and drop empty elements.
  // Need to do this to track what was there originally and try to re-use it.
  while (qs) {
    if (auto c = *qs; c == '&' || c == ';') {
      qs.remove_prefix(1);
      elt_sep = c;
    } else
      break;
  }

  // If there's anything left, look for kv pair.
  if (qs) {
    auto v{qs.clip_prefix_of([](char c) { return c != '&' && c != ';'; })};
    auto k = v.take_prefix_at('=');
    QPair zret{k, v};
    zret.elt_sep = elt_sep;
    if (v.begin() > k.end()) {
      zret.kv_sep = v[-1];
    }
    return zret;
  }
  return {};
}

TextView
query_value_update(Context &ctx, TextView qs, TextView name, Feature const &value, bool case_p, bool force_equal_p)
{
  TextView zret;
  bool nv_is_nil_p = (value.value_type() == NIL);
  if (qs.empty()) {
    if (!nv_is_nil_p) {
      zret = ctx.render_transient([&](BufferWriter &w) { w.print("{}={}", name, std::get<STRING>(value)); });
    }
  } else { // invariant - query string was not empty.
    auto &&[k, v] = ts::query_value_for(qs, name, case_p);
    if (k.empty()) { // not found at all.
      if (!nv_is_nil_p) {
        zret = ctx.render_transient([&](BufferWriter &w) { w.print("{}&{}={}", qs, name, value); });
      }
    } else {
      // Make a note if there was no value but an '=' anyway.
      bool equal_p = force_equal_p || (v.empty() && (v.data() != k.end()));
      // Prefix is the part before the name.
      TextView prefix{qs.data(), k.data()};
      // Suffix is the part after the value.
      TextView suffix{v.end(), qs.end()};
      if (!nv_is_nil_p) {
        zret = ctx.render_transient([&, k = k, v = v](BufferWriter &w) {
          w.write(prefix).write(k);
          if (equal_p || !(value.index() == IndexFor(STRING) && std::get<IndexFor(STRING)>(value).empty())) {
            w.print("={}", value);
          }
          w.write(suffix);
        });
      } else { // NIL, remove the pair.
        prefix.rtrim("&;"_tv);
        suffix.ltrim("&;"_tv);
        if (suffix.empty()) {
          zret = prefix;
        } else if (prefix.empty()) {
          zret = suffix;
        } else { // neither is empty.
          zret = ctx.render_transient([&](BufferWriter &w) { w.print("{}&{}", prefix, suffix); });
        }
      }
    }
  }
  return zret;
}

} // namespace
/* ------------------------------------------------------------------------------------ */
// Query string.
// These have the @c extract method because it can be done as a @c Direct
// value and that's better than running through the formatting.

class QueryValueExtractor : public Extractor
{
public:
  Rv<ActiveType> validate(Config &cfg, Spec &spec, TextView const &arg) override;

  Feature extract(Context &ctx, Spec const &spec) override;

protected:
  TextView _arg;

  /// @return The key (name) for the extractor.
  virtual TextView const &key() const = 0;
  /// @return The appropriate query string.
  virtual TextView query_string(Context &ctx) const = 0;
};

Rv<ActiveType>
QueryValueExtractor::validate(Config &cfg, Spec &spec, const TextView &arg)
{
  if (arg.empty()) {
    return Errata(S_ERROR, "Extractor \"{}\" requires a key name argument.", this->key());
  }
  spec._data.text = cfg.localize(arg);
  return ActiveType{NIL, STRING};
}

Feature
QueryValueExtractor::extract(Context &ctx, const Spec &spec)
{
  auto qs = this->query_string(ctx);
  if (qs.empty()) {
    return NIL_FEATURE;
  }
  auto &&[name, value] = ts::query_value_for(qs, spec._data.text, true); // case insensitive
  if (value.size() == 0) {
    if (value.data() == nullptr || value.data() == name.end()) {
      return NIL_FEATURE; // key not present or value not present
    }
    return FeatureView::Literal("");
  }
  return FeatureView::Direct(value);
}

// --

class Ex_ua_req_query : public StringExtractor
{
public:
  static constexpr TextView NAME{"ua-req-query"};

  Feature extract(Context &ctx, Spec const &spec) override;
  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;
};

Feature
Ex_ua_req_query::extract(Context &ctx, Spec const &)
{
  if (auto hdr{ctx.ua_req_hdr()}; hdr.is_valid()) {
    if (ts::URL url{hdr.url()}; url.is_valid()) {
      return FeatureView::Direct(url.query());
    }
  }
  return NIL_FEATURE;
}

BufferWriter &
Ex_ua_req_query::format(BufferWriter &w, Spec const &spec, Context &ctx)
{
  return bwformat(w, spec, this->extract(ctx, spec));
}

class Ex_ua_req_query_value : public QueryValueExtractor
{
public:
  static constexpr TextView NAME{"ua-req-query-value"};

protected:
  TextView const &key() const override;
  TextView query_string(Context &ctx) const override;
};

TextView const &
Ex_ua_req_query_value::key() const
{
  return NAME;
}

TextView
Ex_ua_req_query_value::query_string(Context &ctx) const
{
  if (auto hdr{ctx.ua_req_hdr()}; hdr.is_valid()) {
    if (ts::URL url{hdr.url()}; url.is_valid()) {
      return url.query();
    }
  }
  return {};
}

// --

class Ex_pre_remap_query : public StringExtractor
{
public:
  static constexpr TextView NAME{"pre-remap-query"};

  Feature extract(Context &ctx, Spec const &spec) override;
  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;
};

Feature
Ex_pre_remap_query::extract(Context &ctx, Spec const &)
{
  if (ts::URL url{ctx._txn.pristine_url_get()}; url.is_valid()) {
    return FeatureView::Direct(url.query());
  }
  return NIL_FEATURE;
}

BufferWriter &
Ex_pre_remap_query::format(BufferWriter &w, Spec const &spec, Context &ctx)
{
  return bwformat(w, spec, this->extract(ctx, spec));
}

class Ex_pre_remap_req_query_value : public QueryValueExtractor
{
public:
  static constexpr TextView NAME{"pre-remap-req-query-value"};

protected:
  TextView const &key() const override;
  TextView query_string(Context &ctx) const override;
};

TextView const &
Ex_pre_remap_req_query_value::key() const
{
  return NAME;
}

TextView
Ex_pre_remap_req_query_value::query_string(Context &ctx) const
{
  if (ts::URL url{ctx._txn.pristine_url_get()}; url.is_valid()) {
    return url.query();
  }
  return {};
}

// --

class Ex_proxy_req_query : public StringExtractor
{
public:
  static constexpr TextView NAME{"proxy-req-query"};

  Feature extract(Context &ctx, Spec const &spec) override;
  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;
};

Feature
Ex_proxy_req_query::extract(Context &ctx, Spec const &)
{
  if (auto hdr{ctx.proxy_req_hdr()}; hdr.is_valid()) {
    if (ts::URL url{hdr.url()}; url.is_valid()) {
      return FeatureView::Direct(url.query());
    }
  }
  return NIL_FEATURE;
}

BufferWriter &
Ex_proxy_req_query::format(BufferWriter &w, Spec const &spec, Context &ctx)
{
  return bwformat(w, spec, this->extract(ctx, spec));
}

class Ex_proxy_req_query_value : public QueryValueExtractor
{
public:
  static constexpr TextView NAME{"proxy-req-query-value"};

protected:
  TextView const &key() const override;
  TextView query_string(Context &ctx) const override;
};

TextView const &
Ex_proxy_req_query_value::key() const
{
  return NAME;
}

TextView
Ex_proxy_req_query_value::query_string(Context &ctx) const
{
  if (auto hdr{ctx.proxy_req_hdr()}; hdr.is_valid()) {
    if (ts::URL url{hdr.url()}; url.is_valid()) {
      return url.query();
    }
  }
  return {};
}

/* ------------------------------------------------------------------------------------ */
/** Sort the query string by name.
 *
 */
class Mod_query_sort : public Modifier
{
  using self_type  = Mod_query_sort;
  using super_type = Modifier;

public:
  static inline const std::string KEY{"query-sort"};
  bool is_valid_for(ActiveType const &ex_type) const override;
  ActiveType result_type(ActiveType const &) const override;
  Rv<Feature> operator()(Context &ctx, feature_type_for<STRING> qs) override;
  static Rv<Handle> load(Config &cfg, YAML::Node node, TextView key, TextView arg, YAML::Node key_value);

protected:
  bool _case_p = false; ///< Case sensitive sort.
  bool _rev_p  = false; ///< Reverse order sort.

  Mod_query_sort(bool case_p, bool rev_p) : _case_p(case_p), _rev_p(rev_p) {}
};

bool
Mod_query_sort::is_valid_for(const ActiveType &ex_type) const
{
  return ex_type.can_satisfy(STRING);
}

ActiveType
Mod_query_sort::result_type(const ActiveType &) const
{
  return {NIL, STRING};
}

Rv<Feature>
Mod_query_sort::operator()(Context &ctx, feature_type_for<STRING> qs)
{
  using list_type = swoc::IntrusiveDList<QPair::Linkage>;
  auto list       = ctx.make<list_type>();

  while (qs) {
    list->append(ctx.make<QPair>(query_take_qpair(qs)));
  }

  // Make an array of pointers to list items to sort.
  size_t length = list->count() - 1; // separators required.
  auto sa       = ctx.alloc_span<QPair *>(list->count());
  auto ptr      = sa.begin();
  for (auto &item : *list) {
    *ptr++  = &item;
    length += item.name.size() + item.value.size() + 1;
  }

  static auto case_sort         = [](QPair *lhs, QPair *rhs) { return strcmp(lhs->name, rhs->name) < 0; };
  static auto case_rev_sort     = [](QPair *lhs, QPair *rhs) { return strcmp(lhs->name, rhs->name) > 0; };
  static auto caseless_sort     = [](QPair *lhs, QPair *rhs) { return strcasecmp(lhs->name, rhs->name) < 0; };
  static auto caseless_rev_sort = [](QPair *lhs, QPair *rhs) { return strcasecmp(lhs->name, rhs->name) > 0; };

  auto sorter = _case_p ? (_rev_p ? case_rev_sort : case_sort) : (_rev_p ? caseless_rev_sort : caseless_sort);

  std::stable_sort(sa.begin(), sa.end(), sorter);

  swoc::FixedBufferWriter w{ctx.transient_buffer(length)};
  bool first_p = true;
  for (auto item : sa) {
    if (!first_p) {
      w.write(item->elt_sep);
    }
    w.print("{}{}{}", item->name, item->kv_sep, item->value);
    first_p = false;
  }
  ctx.transient_finalize(w.view().size());
  return {w.view()};
}

Rv<Modifier::Handle>
Mod_query_sort::load(Config &, YAML::Node, TextView, TextView arg, YAML::Node)
{
  bool case_p = true;
  bool rev_p  = false;
  while (arg) {
    auto token = arg.take_prefix_at(',');
    if (token == ARG_NOCASE) {
      case_p = false;
    } else if (token == ARG_REVERSE) {
      rev_p = true;
    } else {
      return Errata(S_ERROR, R"(Invalid argument "{}" in modifier "{}")", token, KEY);
    }
  }
  return Handle{new self_type(case_p, rev_p)};
}

// ---

class Mod_query_filter : public FilterMod
{
  using self_type  = Mod_query_filter;
  using super_type = Modifier;

public:
  static inline const std::string KEY{"query-filter"};

  static inline const std::string OPT_VALUE         = "value";         ///< Compare value
  static inline const std::string OPT_APPEND        = "append";        ///< Append element.
  static inline const std::string OPT_APPEND_UNIQUE = "append-unique"; ///< Append element uniquely.
  static inline const std::string OPT_PASS_REST     = "pass-rest";     ///< Pass all remainining elements.
  static inline const std::string OPT_DROP_REST     = "drop-rest";     ///< Drop all remainining elements.

  static inline const std::string PAIR_VALUE = "value"; ///< Element value.
  static inline const std::string PAIR_NAME  = "name";  ///< Element name.

  static constexpr TextView CFG_STORE_KEY = "mod-query-filter";
  using cfg_store_type                    = ReservedSpan;

  bool is_valid_for(ActiveType const &ex_type) const override;
  ActiveType result_type(ActiveType const &) const override;
  Rv<Feature> operator()(Context &ctx, feature_type_for<STRING> qs) override;
  static Rv<Handle> load(Config &cfg, YAML::Node node, TextView key, TextView arg, YAML::Node key_value);

  /// Local extractor for name in a pair.
  class Ex_Name : public Extractor
  {
    virtual swoc::Rv<ActiveType> validate(Config &cfg, Spec &spec, swoc::TextView const &) override;
    Feature extract(Context &ctx, Spec const &spec) override;
  };
  /// Local extractor for value in a pair.
  class Ex_Value : public Extractor
  {
    virtual swoc::Rv<ActiveType> validate(Config &cfg, Spec &spec, swoc::TextView const &) override;
    Feature extract(Context &ctx, Spec const &spec) override;
  };

protected:
  /// A filter comparison case.
  struct Case {
    Action _action = PASS; ///< Action on match.

    Comparison::Handle _cmp;       ///< Comparison.
    Comparison::Handle _value_cmp; ///< Comparison for value.

    /// Expressions for name and/or value.
    struct PairExpr {
      Expr _name;
      Expr _value;
    };

    PairExpr _replace; ///< Replacement.

    swoc::MemSpan<PairExpr> _append; ///< Elements to append.

    /// Option for the rest of the elements.
    enum { REST_NONE, REST_PASS, REST_DROP } _opt_rest = REST_NONE;

    void assign(Comparison::Handle &&handle);

    bool operator()(Context &ctx, Feature const &feature);

    Errata pre_load(Config &cfg, YAML::Node node);

    Errata parse_pair(Config &cfg, YAML::Node node, PairExpr &pair);
    void eval_pair(Context &ctx, PairExpr const &pe, QPair &qp) const;
  };

  /// Container for cases with comparisons.
  ComparisonGroup<Case> _cases;

  Mod_query_filter() = default;
  Case const *compare(Context &ctx, QPair const &qp) const;

  static inline Ex_Name ex_name;
  static inline Ex_Value ex_value;
  /// Table for local extactors.
  static Extractor::Table _ex_table;
};
Extractor::Table Mod_query_filter::_ex_table{
  {"name",  &Mod_query_filter::ex_name },
  {"value", &Mod_query_filter::ex_value}
};

swoc::Rv<ActiveType>
Mod_query_filter::Ex_Name::validate(Config &cfg, Extractor::Spec &spec, TextView const &)
{
  spec._data.ctx_reserved_span = *(cfg.named_object<cfg_store_type>(CFG_STORE_KEY));
  return ActiveType{NIL, STRING};
}

Feature
Mod_query_filter::Ex_Name::extract(Context &ctx, Extractor::Spec const &spec)
{
  auto qp = ctx.storage_for(spec._data.ctx_reserved_span).rebind<QPair *>()[0];
  return FeatureView::Literal(qp->name);
}

swoc::Rv<ActiveType>
Mod_query_filter::Ex_Value::validate(Config &cfg, Extractor::Spec &spec, TextView const &)
{
  spec._data.ctx_reserved_span = *(cfg.named_object<cfg_store_type>(CFG_STORE_KEY));
  return ActiveType{NIL, STRING};
}

Feature
Mod_query_filter::Ex_Value::extract(Context &ctx, Extractor::Spec const &spec)
{
  auto qp = ctx.storage_for(spec._data.ctx_reserved_span).rebind<QPair *>()[0];
  return FeatureView::Literal(qp->value);
}

bool
Mod_query_filter::is_valid_for(const ActiveType &ex_type) const
{
  return ex_type.can_satisfy(STRING);
}

ActiveType
Mod_query_filter::result_type(const ActiveType &) const
{
  return {NIL, STRING};
}

auto
Mod_query_filter::compare(Context &ctx, QPair const &qp) const -> Case const *
{
  using fstr = feature_type_for<STRING>;
  for (auto const &c : _cases) {
    if ((!c._cmp || (*c._cmp)(ctx, fstr(qp.name))) && (!c._value_cmp || (*c._value_cmp)(ctx, fstr(qp.value)))) {
      return &c;
    }
  }
  return nullptr;
}

void
Mod_query_filter::Case::assign(Comparison::Handle &&handle)
{
  _cmp = std::move(handle);
}

swoc::Errata
Mod_query_filter::Case::parse_pair(Config &cfg, YAML::Node node, PairExpr &pair)
{
  if (!node.IsMap()) {
    return Errata(S_ERROR, "Element at {} is not an object as required.", node.Mark());
  }

  if (auto knode = node[PAIR_NAME]; knode) {
    auto &&[expr, errata]{cfg.parse_expr(knode)};
    if (!errata.is_ok()) {
      errata.note("While parsing expression for {}.", PAIR_NAME);
    }
    pair._name = std::move(expr);
  }

  if (auto vnode = node[PAIR_VALUE]; vnode) {
    auto &&[expr, errata]{cfg.parse_expr(vnode)};
    if (!errata.is_ok()) {
      errata.note("While parsing expression for {}.", PAIR_VALUE);
    }
    pair._value = std::move(expr);
  }

  return {};
}

Errata
Mod_query_filter::Case::pre_load(Config &cfg, YAML::Node cmp_node)
{
  unsigned action_count = 0;

  if (!cmp_node.IsMap()) {
    return Errata(S_ERROR, "List element at {} for {} modifier is not a comparison object.", cmp_node.Mark(), KEY);
  }

  if (auto do_node = cmp_node[Global::DO_KEY]; do_node) {
    return Errata(S_ERROR, R"("{}" at line {} is not allowed in a modifier comparison.)", Global::DO_KEY, do_node.Mark());
  }

  YAML::Node drop_node = cmp_node[ACTION_DROP];
  if (drop_node) {
    _action = DROP;
    cmp_node.remove(ACTION_DROP);
    ++action_count;
  }

  if (YAML::Node pass_node = cmp_node[ACTION_PASS]; pass_node) {
    _action = PASS;
    cmp_node.remove(ACTION_PASS);
    ++action_count;
  }

  if (YAML::Node replace_node = cmp_node[ACTION_REPLACE]; replace_node) {
    Errata errata = this->parse_pair(cfg, replace_node, _replace);
    if (!errata.is_ok()) {
      errata.note("While parsing expression at {} for {} key in comparison at {}.", replace_node.Mark(), ACTION_REPLACE,
                  cmp_node.Mark());
      return errata;
    }
    _action = REPLACE;
    cmp_node.remove(ACTION_REPLACE);
    ++action_count;
  }

  if (action_count > 1) {
    return Errata(S_ERROR, "Only one of {}, {}, {} is allowed in the {} comparison at {}.", ACTION_REPLACE, ACTION_DROP,
                  ACTION_PASS, KEY, cmp_node.Mark());
  }

  YAML::Node opt_node = cmp_node[ACTION_OPT];
  if (opt_node) {
    if (!opt_node.IsMap()) {
      return Errata(S_ERROR, R"("Value for "{}" at {} for "{}" modifier is not an object.)", ACTION_OPT, opt_node.Mark(), KEY);
    }

    if (YAML::Node vcmp_node = opt_node[OPT_VALUE]; vcmp_node) {
      auto &&[vcmp, vcmp_errata]{Comparison::load(cfg, vcmp_node)};
      if (!vcmp_errata.is_ok()) {
        return std::move(vcmp_errata.note(R"(While parsing "{}" option for "{}" modifier)", OPT_VALUE, KEY));
      }
      _value_cmp = std::move(vcmp);
      opt_node.remove(vcmp_node);
    }

    Errata errata;
    if (YAML::Node append_node = opt_node[OPT_APPEND]; append_node) {
      if (append_node.IsSequence()) {
        auto span = cfg.alloc_span<PairExpr>(append_node.size());
        for (unsigned idx = 0; idx < append_node.size() && errata.is_ok(); ++idx) {
          auto &p{span[idx]};
          new (&p) PairExpr;
          errata = this->parse_pair(cfg, append_node[idx], p);
        }
      } else {
        auto span = cfg.alloc_span<PairExpr>(1);
        new (&span[0]) PairExpr;
        errata = this->parse_pair(cfg, append_node, span[0]);
      }
      opt_node.remove(append_node);
    }
    if (!errata.is_ok()) {
      errata.note("While parsing {} expressions.", OPT_APPEND);
      return errata;
    }

    if (auto pass_rest_node = opt_node[OPT_PASS_REST]; pass_rest_node) {
      _opt_rest = Case::REST_PASS;
      opt_node.remove(pass_rest_node);
    }

    if (auto drop_rest_node = opt_node[OPT_DROP_REST]; drop_rest_node) {
      if (_opt_rest != Case::REST_NONE) {
        return Errata(S_ERROR, "{} at {} has both {} and {} which is not allowed.", ACTION_OPT, opt_node.Mark(), OPT_PASS_REST,
                      OPT_DROP_REST);
      }
      _opt_rest = Case::REST_DROP;
      opt_node.remove(drop_rest_node);
    }

    cmp_node.remove(opt_node);
  }

  return {};
}

Rv<Modifier::Handle>
Mod_query_filter::load(Config &cfg, YAML::Node node, TextView, TextView, YAML::Node key_value)
{
  auto self = new self_type;
  Handle handle(self);
  let local_ex_scope{cfg._local_extractors, &_ex_table};

  // Need reserved context storage to pass the current @c QPair down to nested extractors.
  // The reserved span is stored in the configuration and then used at run time.
  auto ctx_store = cfg.obtain_named_object<cfg_store_type>(CFG_STORE_KEY);
  if (ctx_store->n == 0) {
    *ctx_store = cfg.reserve_ctx_storage(sizeof(QPair *));
  }

  if (auto errata = self->_cases.load(cfg, key_value); !errata.is_ok()) {
    errata.note(R"(While parsing modifier "{}" at line {}.)", KEY, node.Mark());
    return errata;
  }

  return handle;
}

void
Mod_query_filter::Case::eval_pair(Context &ctx, const PairExpr &pe, QPair &qp) const
{
  if (!pe._name.empty()) {
    auto name = ctx.extract(pe._name);
    if (!is_nil(name)) {
      qp.name = ctx.render_transient([&](BufferWriter &w) { w.print("{}", name); });
      ctx.commit_transient();
    }
  }

  if (!pe._value.empty()) {
    auto value = ctx.extract(pe._value);
    if (!is_nil(value)) {
      qp.value = ctx.render_transient([&](BufferWriter &w) { w.print("{}", value); });
      ctx.commit_transient();
    }
  }
}

Rv<Feature>
Mod_query_filter::operator()(Context &ctx, feature_type_for<STRING> qs)
{
  using list_type = swoc::IntrusiveDList<QPair::Linkage>;
  auto list       = ctx.make<list_type>();
  QPair *&qp_ex   = ctx.storage_for(*ctx.cfg().named_object<cfg_store_type>(CFG_STORE_KEY)).rebind<QPair *>()[0];

  while (qs) {
    auto qp       = query_take_qpair(qs);
    auto c        = this->compare(ctx, qp);
    Action action = (c ? c->_action : DROP);
    qp_ex         = &qp; // pass to potential extractors.
    switch (action) {
    case DROP:
      break;
    case PASS:
      list->append(ctx.make<QPair>(qp));
      break;
    case REPLACE:
      c->eval_pair(ctx, c->_replace, qp);
      list->append(ctx.make<QPair>(qp));
      break;
    }

    // Append action.
    for (auto &pe : c->_append) {
      QPair q_pair;
      c->eval_pair(ctx, pe, q_pair);
      list->append(ctx.make<QPair>(q_pair));
    }

    // drop-rest, pass-rest actions.
    if (c->_opt_rest != Case::REST_NONE) {
      if (c->_opt_rest == Case::REST_DROP) {
        qs.clear();
      }
      break;
    }
  }

  // Consolidate list into a new query string.
  size_t length = qs.size() + list->count() + 1; // separators.
  for (auto &p : *list) {
    length += p.name.size() + p.value.size() + 1;
  }

  swoc::FixedBufferWriter w{ctx.transient_buffer(length)};
  bool first_p = true;
  for (auto &item : *list) {
    if (!first_p) {
      w.write(item.elt_sep);
    }
    w.print("{}{}{}", item.name, swoc::bwf::Optional("{}", item.kv_sep), item.value);
    first_p = false;
  }
  w.write(qs);
  ctx.transient_finalize(w.view().size());
  return {FeatureView(w.view())};
}
/* ------------------------------------------------------------------------------------ */
/** Set the query for the request.
 */
class Do_ua_req_query : public Directive
{
  using super_type = Directive;       ///< Parent type.
  using self_type  = Do_ua_req_query; ///< Self reference type.
public:
  static const std::string KEY; ///< Directive name.
  static const HookMask HOOKS;  ///< Valid hooks for directive.

  explicit Do_ua_req_query(Expr &&expr);

  Errata invoke(Context &ctx) override;

  static Rv<Handle> load(Config &cfg, CfgStaticData const *rtti, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

protected:
  Expr _expr; ///< Host feature.
};

const std::string Do_ua_req_query::KEY{"ua-req-query"};
const HookMask Do_ua_req_query::HOOKS{MaskFor({Hook::CREQ, Hook::PRE_REMAP, Hook::REMAP, Hook::POST_REMAP})};

Do_ua_req_query::Do_ua_req_query(Expr &&expr) : _expr(std::move(expr)) {}

Errata
Do_ua_req_query::invoke(Context &ctx)
{
  TextView text{std::get<IndexFor(STRING)>(ctx.extract(_expr))};
  if (auto hdr{ctx.ua_req_hdr()}; hdr.is_valid()) {
    hdr.url().query_set(text);
  }
  return {};
}

swoc::Rv<Directive::Handle>
Do_ua_req_query::load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &, swoc::TextView const &,
                      YAML::Node key_value)
{
  auto &&[expr, errata]{cfg.parse_expr(key_value)};
  if (!errata.is_ok()) {
    errata.note(R"(While parsing "{}" directive at {}.)", KEY, drtv_node.Mark());
    return std::move(errata);
  }
  if (expr.is_null()) {
    expr = Feature{FeatureView::Literal(""_tv)};
  }
  if (!expr.result_type().can_satisfy(STRING)) {
    return Errata(S_ERROR, R"(Value for "{}" directive at {} must be a string.)", KEY, drtv_node.Mark());
  }
  return Handle(new self_type(std::move(expr)));
}
/* ------------------------------------------------------------------------------------ */
/** Set the query for the proxy request.
 */
class Do_proxy_req_query : public Directive
{
  using super_type = Directive;          ///< Parent type.
  using self_type  = Do_proxy_req_query; ///< Self reference type.
public:
  static const std::string KEY; ///< Directive name.
  static const HookMask HOOKS;  ///< Valid hooks for directive.

  explicit Do_proxy_req_query(Expr &&fmt);

  Errata invoke(Context &ctx) override;

  static Rv<Handle> load(Config &cfg, CfgStaticData const *rtti, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

protected:
  Expr _fmt; ///< Host feature.
};

const std::string Do_proxy_req_query::KEY{"proxy-req-query"};
const HookMask Do_proxy_req_query::HOOKS{MaskFor({Hook::PREQ})};

Do_proxy_req_query::Do_proxy_req_query(Expr &&fmt) : _fmt(std::move(fmt)) {}

Errata
Do_proxy_req_query::invoke(Context &ctx)
{
  TextView text{std::get<IndexFor(STRING)>(ctx.extract(_fmt))};
  if (auto hdr{ctx.ua_req_hdr()}; hdr.is_valid()) {
    hdr.url().query_set(text);
  }
  return {};
}

swoc::Rv<Directive::Handle>
Do_proxy_req_query::load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &, swoc::TextView const &,
                         YAML::Node key_value)
{
  auto &&[expr, errata]{cfg.parse_expr(key_value)};
  if (!errata.is_ok()) {
    errata.note(R"(While parsing "{}" directive at {}.)", KEY, drtv_node.Mark());
    return std::move(errata);
  }
  if (!expr.result_type().can_satisfy(STRING)) {
    return Errata(S_ERROR, R"(Value for "{}" directive at {} must be a string.)", KEY, drtv_node.Mark());
  }
  return Handle(new self_type(std::move(expr)));
}
/* ------------------------------------------------------------------------------------ */
class QueryValueDirective : public Directive
{
  using self_type  = QueryValueDirective;
  using super_type = Directive;

public:
  TextView _name; ///< Query value key name.
  Expr _expr;     ///< Replacement value.

  /** Base constructor.
   *
   * @param name Name of the field.
   * @param expr Value to assign to the field.
   */
  QueryValueDirective(TextView const &name, Expr &&expr);

  /** Load from configuration.
   *
   * @param cfg Configuration.
   * @param maker Subclass maker.
   * @param key Name of the key identifying the directive.
   * @param name Field name (directive argumnet).
   * @param key_value  Value of the node for @a key.
   * @return An instance of the directive for @a key, or errors.
   */
  static Rv<Handle> load(Config &cfg, std::function<Handle(TextView const &name, Expr &&fmt)> const &maker, TextView const &key,
                         TextView const &arg, YAML::Node key_value);

  /** Invoke the directive on a URL.
   *
   * @param ctx Runtime context.
   * @param url URL.
   * @return Errors, if any.
   */
  Errata invoke_on_url(Context &ctx, ts::URL &&url);

protected:
  /** Get the directive name (key).
   *
   * @return The directive key.
   *
   * Used by subclasses to provide the key for diagnostics.
   */
  virtual swoc::TextView key() const = 0;
};

QueryValueDirective::QueryValueDirective(TextView const &name, Expr &&expr) : _name(name), _expr(std::move(expr)) {}

auto
QueryValueDirective::load(Config &cfg, std::function<Handle(const TextView &, Expr &&)> const &maker, TextView const &key,
                          TextView const &arg, YAML::Node key_value) -> Rv<Handle>
{
  auto &&[expr, errata]{cfg.parse_expr(key_value)};
  if (!errata.is_ok()) {
    errata.note(R"(While parsing value for "{}".)", key);
    return std::move(errata);
  }

  auto expr_type = expr.result_type();
  if (!expr_type.has_value()) {
    return Errata(S_ERROR, R"(Directive "{}" must have a value.)", key);
  }
  return maker(cfg.localize(arg), std::move(expr));
}

Errata
QueryValueDirective::invoke_on_url(Context &ctx, ts::URL &&url)
{
  if (url.is_valid()) {
    auto qs = query_value_update(ctx, url.query(), _name, ctx.extract(_expr), true, false);
    url.query_set(qs);
    ctx.transient_discard();
  }
  return Errata(S_ERROR, "Failed to update query value {} because the URL could not be found.", _name);
}

// --
class Do_ua_req_query_value : public QueryValueDirective
{
  using self_type  = Do_ua_req_query_value;
  using super_type = QueryValueDirective;

public:
  static inline const std::string KEY{"ua-req-query-value"}; ///< Directive key.
  static inline const HookMask HOOKS{
    MaskFor(Hook::CREQ, Hook::PRE_REMAP, Hook::REMAP, Hook::POST_REMAP)}; ///< Valid hooks for directive.

  using super_type::invoke;
  Errata invoke(Context &ctx) override;

  static Rv<Handle> load(Config &cfg, CfgStaticData const *rtti, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

protected:
  using super_type::super_type; // Inherit super_type constructors.
  TextView key() const override;
};

TextView
Do_ua_req_query_value::key() const
{
  return KEY;
}

Errata
Do_ua_req_query_value::invoke(Context &ctx)
{
  return this->invoke_on_url(ctx, ctx.ua_req_hdr().url());
}

Rv<Directive::Handle>
Do_ua_req_query_value::load(Config &cfg, CfgStaticData const *, YAML::Node, swoc::TextView const &, swoc::TextView const &arg,
                            YAML::Node key_value)
{
  return super_type::load(
    cfg, [](TextView const &name, Expr &&fmt) -> Handle { return Handle(new self_type(name, std::move(fmt))); }, KEY, arg,
    key_value);
}

// --
class Do_proxy_req_query_value : public QueryValueDirective
{
  using self_type  = Do_proxy_req_query_value;
  using super_type = QueryValueDirective;

public:
  static inline const std::string KEY{"proxy-req-query-value"}; ///< Directive key.
  static inline const HookMask HOOKS{MaskFor(Hook::PREQ)};      ///< Valid hooks for directive.

  using super_type::invoke;
  Errata invoke(Context &ctx) override;

  static Rv<Handle> load(Config &cfg, CfgStaticData const *rtti, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

protected:
  using super_type::super_type; // Inherit super_type constructors.
  TextView key() const override;
};

TextView
Do_proxy_req_query_value::key() const
{
  return KEY;
}

Errata
Do_proxy_req_query_value::invoke(Context &ctx)
{
  return this->invoke_on_url(ctx, ctx.proxy_req_hdr().url());
}

Rv<Directive::Handle>
Do_proxy_req_query_value::load(Config &cfg, CfgStaticData const *, YAML::Node, swoc::TextView const &, swoc::TextView const &arg,
                               YAML::Node key_value)
{
  return super_type::load(
    cfg, [](TextView const &name, Expr &&fmt) -> Handle { return Handle(new self_type(name, std::move(fmt))); }, KEY, arg,
    key_value);
}
/* ------------------------------------------------------------------------------------ */
namespace
{
Ex_ua_req_query ua_req_query;
Ex_proxy_req_query proxy_req_query;
Ex_pre_remap_query pre_remap_query;

Ex_ua_req_query_value ua_req_query_value;
Ex_pre_remap_query pre_remap_req_query_value;
Ex_proxy_req_query_value proxy_req_query_value;

[[maybe_unused]] bool INITIALIZED = []() -> bool {
  Extractor::define(Ex_ua_req_query::NAME, &ua_req_query);
  Extractor::define(Ex_pre_remap_query::NAME, &pre_remap_query);
  Extractor::define(Ex_proxy_req_query::NAME, &proxy_req_query);

  Extractor::define(Ex_ua_req_query_value::NAME, &ua_req_query_value);
  Extractor::define(Ex_pre_remap_req_query_value::NAME, &ua_req_query_value);
  Extractor::define(Ex_proxy_req_query_value::NAME, &ua_req_query_value);

  Modifier::define<Mod_query_sort>();
  Modifier::define<Mod_query_filter>();

  Config::define<Do_ua_req_query>();
  Config::define<Do_ua_req_query_value>();
  Config::define<Do_proxy_req_query>();
  Config::define<Do_proxy_req_query_value>();

  return true;
}();
} // namespace
