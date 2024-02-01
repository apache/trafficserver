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
#include <map>
#include <numeric>
#include <glob.h>

#include <swoc/TextView.h>
#include <swoc/MemSpan.h>
#include <swoc/swoc_file.h>
#include <swoc/bwf_std.h>
#include <swoc/Scalar.h>
#include <yaml-cpp/yaml.h>

#include "txn_box/Directive.h"
#include "txn_box/Extractor.h"
#include "txn_box/Modifier.h"
#include "txn_box/Expr.h"
#include "txn_box/Config.h"
#include "txn_box/Context.h"

#include "txn_box/ts_util.h"
#include "txn_box/yaml_util.h"

using swoc::TextView;
using swoc::Errata;
using swoc::Rv;
using swoc::BufferWriter;
namespace bwf = swoc::bwf;
using namespace swoc::literals;

static constexpr char ARG_PREFIX = '<';
static constexpr char ARG_SUFFIX = '>';

/* ------------------------------------------------------------------------------------ */
swoc::Lexicon<Hook> HookName{
  {{Hook::POST_LOAD, {"post-load"}},
   {Hook::TXN_START, {"txn-open"}},
   {Hook::CREQ, {"ua-req", "creq", "client-req"}},
   {Hook::PREQ, {"proxy-req", "preq", "upstream-req", "up-req"}},
   {Hook::URSP, {"upstream-rsp", "ursp", "up-rsp"}},
   {Hook::PRSP, {"proxy-rsp", "prsp"}},
   {Hook::PRE_REMAP, {"pre-remap"}},
   {Hook::POST_REMAP, {"post-remap"}},
   {Hook::TXN_CLOSE, {"txn-close"}},
   {Hook::REMAP, {"remap"}},
   {Hook::MSG, {"msg"}}}
};

std::array<TSHttpHookID, std::tuple_size<Hook>::value> TS_Hook;

BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &spec, Hook hook)
{
  if (spec.has_numeric_type()) {
    return bwformat(w, spec, IndexFor(hook));
  }
  return bwformat(w, spec, HookName[hook]);
}

namespace
{
[[maybe_unused]] bool INITIALIZED = []() -> bool {
  HookName.set_default(Hook::INVALID);

  TS_Hook[IndexFor(Hook::TXN_START)]  = TS_HTTP_TXN_START_HOOK;
  TS_Hook[IndexFor(Hook::CREQ)]       = TS_HTTP_READ_REQUEST_HDR_HOOK;
  TS_Hook[IndexFor(Hook::PREQ)]       = TS_HTTP_SEND_REQUEST_HDR_HOOK;
  TS_Hook[IndexFor(Hook::URSP)]       = TS_HTTP_READ_RESPONSE_HDR_HOOK;
  TS_Hook[IndexFor(Hook::PRSP)]       = TS_HTTP_SEND_RESPONSE_HDR_HOOK;
  TS_Hook[IndexFor(Hook::PRE_REMAP)]  = TS_HTTP_PRE_REMAP_HOOK;
  TS_Hook[IndexFor(Hook::POST_REMAP)] = TS_HTTP_POST_REMAP_HOOK;
  TS_Hook[IndexFor(Hook::TXN_CLOSE)]  = TS_HTTP_TXN_CLOSE_HOOK;

  return true;
}();
}; // namespace

swoc::Lexicon<ValueType> const ValueTypeNames{
  {{ValueType::NIL, "nil"},
   {ValueType::STRING, "string"},
   {ValueType::INTEGER, "integer"},
   {ValueType::BOOLEAN, "boolean"},
   {ValueType::FLOAT, "float"},
   {ValueType::IP_ADDR, "IP address"},
   {ValueType::DURATION, "duration"},
   {ValueType::TIMEPOINT, "time point"},
   {ValueType::CONS, "cons"},
   {ValueType::TUPLE, "tuple"},
   {ValueType::GENERIC, "generic"}}
};

// --------------------------------------------------------------------------
Config::~Config()
{
  // Invoke all the finalizers to do additional cleanup.
  for (auto &&f : _finalizers) {
    f._f(f._ptr);
    std::destroy_at(&f._f); // clean up the cleaner too, just in case.
  }
}

template <typename F> struct on_scope_exit {
  F _f;
  explicit on_scope_exit(F &&f) : _f(std::move(f)) {}
  ~on_scope_exit() { _f(); }
};

swoc::Rv<swoc::TextView>
parse_arg(TextView &key)
{
  TextView arg{key};
  TextView name{arg.take_prefix_at(ARG_PREFIX)};
  if (name.size() == key.size()) { // no arg prefix, it's just the name.
    return {};
  }
  if (arg.empty() || arg.back() != ARG_SUFFIX) {
    return Errata(S_ERROR, R"(Argument for "{}" is not properly terminated with '{}'.)", name, ARG_SUFFIX);
  }
  key = name;
  return arg.remove_suffix(1);
}

/* ------------------------------------------------------------------------------------ */
Config::Config() : _arena(_cfg_storage_required + 2048)
{
  _cfg_store = _arena.alloc(_cfg_storage_required);
  // Set up the run time type information for the directives.
  _drtv_info = this->alloc_span<Directive::CfgStaticData>(_factory.size());
  for (auto const &[name, factory_info] : _factory) {
    auto &di = _drtv_info[factory_info._idx];
    new (&di) Directive::CfgStaticData;
    di._static = &factory_info;
  }
}

swoc::MemSpan<void>
Config::allocate_cfg_storage(size_t n, size_t align)
{
  if (align == 1) {
    return _arena.alloc(n);
  }

  // Need to add this logic to @c MemArena - shouldn't have to do it here.
  // Should only loop twice at most - if it doesn't return it requires the arena to have
  // enough free space to work on the second try.
  while (true) {
    auto span = _arena.remnant();
    if (auto remainder = reinterpret_cast<uintptr_t>(span.data()) % align; remainder) {
      auto pad = align - remainder;
      if (span.size() >= n + pad) {
        return _arena.alloc(n + pad).remove_prefix(pad);
      }
      _arena.require(n + pad); // Guarantee enough space to align correctly.
    } else if (span.size() >= n) {
      return _arena.alloc(n); // already aligned and big enough.
    } else {
      _arena.require(n + align); // bump up to make sure new space can be aligned.
    }
  }
}

ReservedSpan
Config::reserve_ctx_storage(size_t n)
{
  using Align = swoc::Scalar<8>;
  // Pre-block to store status of the reserved memory.
  _ctx_storage_required += Align(swoc::round_up(sizeof(Context::ReservedStatus)));
  // The actual reservation.
  ReservedSpan span{_ctx_storage_required, n};
  _ctx_storage_required += Align(swoc::round_up(n));
  return span;
}

Config::self_type &
Config::localize(Feature &feature)
{
  std::visit([&](auto &t) { this->localize(t); }, static_cast<Feature::variant_type &>(feature));
  return *this;
}

TextView &
Config::localize(TextView &text, LocalOpt opt)
{
  if (text.size()) {
    if (LOCAL_CSTR == opt) {
      auto span{_arena.alloc(text.size() + 1).rebind<char>()};
      memcpy(span, text);
      span[text.size()] = 0;
      text              = span.subspan(0, text.size());
    } else {
      auto span{_arena.alloc(text.size()).rebind<char>()};
      memcpy(span, text);
      text = span;
    }
  }
  return text;
};

Rv<ActiveType>
Config::validate(Extractor::Spec &spec)
{
  if (spec._name.empty()) {
    return Errata(S_ERROR, R"(Extractor name required but not found.)");
  }

  if (spec._idx < 0) {
    auto name = TextView{spec._name};
    auto &&[arg, arg_errata]{parse_arg(name)};
    if (!arg_errata.is_ok()) {
      return std::move(arg_errata);
    }

    Extractor *ex = nullptr;
    if (_local_extractors) {
      if (auto spot = _local_extractors->find(name); spot != _local_extractors->end()) {
        ex = spot->second;
      }
    }

    if (nullptr == ex) {
      ex = Extractor::find(name);
    }

    if (nullptr != ex) {
      spec._exf  = ex;
      spec._name = this->localize(name);
      spec._ext  = this->localize(spec._ext);
      auto &&[vt, errata]{ex->validate(*this, spec, arg)};
      if (!errata.is_ok()) {
        return std::move(errata);
      }
      return vt;
    }
    return Errata(S_ERROR, R"(Extractor "{}" not found.)", name);
  }
  return {STRING}; // non-negative index => capture group => always a string
}

Rv<Expr>
Config::parse_unquoted_expr(swoc::TextView const &text)
{
  // Integer?
  TextView parsed;
  auto n = swoc::svtoi(text, &parsed);
  if (parsed.size() == text.size()) {
    return Expr{Feature{n}};
  }

  // bool?
  auto b = BoolNames[text];
  if (b != BoolTag::INVALID) {
    return Expr{Feature{b == BoolTag::True}};
  }

  // float?
  double f = swoc::svtod(text, &parsed);
  if (parsed.size() == text.size()) {
    return Expr{Feature{f}};
  }

  // IP Address?
  swoc::IPAddr addr;
  if (addr.load(text)) {
    return Expr{Feature{addr}};
  }

  // Presume an extractor.
  Extractor::Spec spec;
  bool valid_p = spec.parse(text);
  if (!valid_p) {
    return Errata(S_ERROR, R"(Invalid syntax for extractor "{}" - not a valid specifier.)", text);
  }
  auto &&[vt, errata] = this->validate(spec);
  if (!errata.is_ok()) {
    return std::move(errata);
  }

  if (vt.is_cfg_const()) {
    return Expr{spec._exf->extract(*this, spec)};
  }

  return Expr{spec, vt};
}

Rv<Expr>
Config::parse_composite_expr(TextView const &text)
{
  ActiveType single_vt;
  auto parser{swoc::bwf::Format::bind(text)};
  std::vector<Extractor::Spec> specs; // hold the specifiers during parse.
  // Used to handle literals in @a format_string. Can't be const because it must be updated
  // for each literal.
  Extractor::Spec literal_spec;

  literal_spec._type = Extractor::Spec::LITERAL_TYPE;

  while (parser) {
    Extractor::Spec spec;
    std::string_view literal;
    bool spec_p = false;
    try {
      spec_p = parser(literal, spec);
    } catch (std::exception const &exp) {
      return Errata(S_ERROR, "Invalid syntax - {}", exp.what());
    } catch (...) {
      return Errata(S_ERROR, "Invalid syntax.");
    }

    if (!literal.empty()) { // leading literal text, add as specifier.
      literal_spec._ext = this->localize(literal, LOCAL_CSTR);
      specs.push_back(literal_spec);
    }

    if (spec_p) { // specifier present.
      if (spec._idx >= 0) {
        specs.push_back(spec); // group reference, store it.
      } else {
        auto &&[vt, errata] = this->validate(spec);
        if (errata.is_ok()) {
          single_vt = vt; // Save for singleton case.
          specs.push_back(spec);
        } else {
          errata.note(R"(While parsing specifier at offset {}.)", text.size() - parser._fmt.size());
          return std::move(errata);
        }
      }
    }
  }

  // If it is a singleton, return it as one of the singleton types.
  if (specs.size() == 1) {
    if (specs[0]._exf) {
      return Expr{specs[0], single_vt};
    } else if (specs[0]._type == Extractor::Spec::LITERAL_TYPE) {
      FeatureView f{specs[0]._ext};
      f._literal_p = true; // because it was localized when parsed from the composite.
      f._cstr_p    = true; // and automatically null terminated.
      return Expr{f};
    }
    // else it's an indexed specifier, treat as a composite.
  }
  // Multiple specifiers, check for overall properties.
  Expr expr;
  auto &cexpr  = expr._raw.emplace<Expr::COMPOSITE>();
  cexpr._specs = std::move(specs);
  for (auto const &s : specs) {
    expr._max_arg_idx = std::max(expr._max_arg_idx, s._idx);
  }

  return expr;
}

Rv<Expr>
Config::parse_scalar_expr(YAML::Node node)
{
  Rv<Expr> zret;
  TextView text{node.Scalar()};
  if (node.IsNull()) {
    return Expr{};
  } else if (node.Tag() == "?"_tv) { // unquoted, must be extractor.
    zret = this->parse_unquoted_expr(text);
  } else {
    zret = this->parse_composite_expr(text);
  }

  if (zret.is_ok()) {
    auto &expr = zret.result();
    if (expr._max_arg_idx >= 0) {
      if (_active_capture._count == 0) {
        return Errata(S_ERROR, R"(Regular expression capture group used at {} but no regular expression is active.)", node.Mark());
      } else if (expr._max_arg_idx >= int(_active_capture._count)) {
        return Errata(
          S_ERROR,
          R"(Regular expression capture group {} used at {} but the maximum capture group is {} in the active regular expression from line {}.)",
          expr._max_arg_idx, node.Mark(), _active_capture._count - 1, _active_capture._line);
      }
    }
  }
  return zret;
}

Rv<Expr>
Config::parse_expr_with_mods(YAML::Node node)
{
  auto &&[expr, expr_errata]{this->parse_expr(node[0])};
  if (!expr_errata.is_ok()) {
    expr_errata.note("While processing the expression at {}.", node.Mark());
    return std::move(expr_errata);
  }
  auto scope{this->feature_scope(expr.result_type())};
  for (unsigned idx = 1; idx < node.size(); ++idx) {
    auto child{node[idx]};
    auto &&[mod, mod_errata]{Modifier::load(*this, child, expr.result_type())};
    if (!mod_errata.is_ok()) {
      mod_errata.note(R"(While parsing feature expression at {}.)", child.Mark(), node.Mark());
      return std::move(mod_errata);
    }
    _active_feature._type = mod->result_type(_active_feature._type);
    expr._mods.emplace_back(std::move(mod));
  }

  return std::move(expr);
}

Rv<Expr>
Config::parse_expr(YAML::Node expr_node)
{
  std::string_view expr_tag(expr_node.Tag());

  // This is the base entry method, so it needs to handle all cases, although most of them
  // will be delegated. Handle the direct / simple special cases here.

  if (expr_node.IsNull()) { // explicit NULL
    return Expr{NIL_FEATURE};
  }

  // If explicitly marked a literal, then no further processing should be done.
  if (0 == strcasecmp(expr_tag, LITERAL_TAG)) {
    if (!expr_node.IsScalar()) {
      return Errata(S_ERROR, R"("!{}" tag used on value at {} which is not a string as required for a literal.)", LITERAL_TAG,
                    expr_node.Mark());
    }
    return Expr{FeatureView::Literal(this->localize(expr_node.Scalar()))};
  } else if (0 == strcasecmp(expr_tag, DURATION_TAG)) {
    if (!expr_node.IsScalar()) {
      return Errata(S_ERROR, R"("!{}" tag used on value at {} which is not a string as required for a literal.)", LITERAL_TAG,
                    expr_node.Mark());
    }
    auto &&[dt, dt_errata]{Feature{expr_node.Scalar()}.as_duration()};
    return {Expr(dt), std::move(dt_errata)};
  } else if (0 != strcasecmp(expr_tag, "?"_sv) && 0 != strcasecmp(expr_tag, "!"_sv)) {
    return Errata(S_ERROR, R"("{}" tag for extractor expression is not supported.)", expr_tag);
  }

  if (expr_node.IsScalar()) {
    return this->parse_scalar_expr(expr_node);
  }
  if (!expr_node.IsSequence()) {
    return Errata(S_ERROR, "Feature expression is not properly structured.");
  }

  // It's a sequence, handle the various cases.
  if (expr_node.size() == 0) {
    return Expr{NIL_FEATURE};
  }
  if (expr_node.size() == 1) {
    return this->parse_scalar_expr(expr_node[0]);
  }

  if (expr_node[1].IsMap()) { // base expression with modifiers.
    return this->parse_expr_with_mods(expr_node);
  }

  // Else, after all this, it's a tuple, treat each element as an expression.
  ActiveType l_types;
  bool literal_p = true; // Is the entire sequence literal?
  std::vector<Expr> xa;
  xa.reserve(expr_node.size());
  for (auto const &child : expr_node) {
    auto &&[expr, errata]{this->parse_expr(child)};
    if (!errata.is_ok()) {
      errata.note("While parsing feature expression list at {}.", expr_node.Mark());
      return std::move(errata);
    }
    l_types |= expr.result_type().base_types();
    if (!expr.is_literal()) {
      literal_p = false;
    }
    xa.emplace_back(std::move(expr));
  }

  Expr expr;
  if (literal_p) {
    FeatureTuple t = this->alloc_span<Feature>(xa.size());
    unsigned idx   = 0;
    for (auto &f : t) {
      f = std::get<Expr::LITERAL>(xa[idx++]._raw);
    }
    expr._raw = t;
  } else {
    auto &list  = expr._raw.emplace<Expr::LIST>();
    list._types = l_types;
    list._exprs = std::move(xa);
  }
  return expr;
}

Rv<Directive::Handle>
Config::load_directive(YAML::Node const &drtv_node)
{
  YAML::Node key_node;
  for (auto const &[key_name, key_value] : drtv_node) {
    TextView name{key_name.Scalar()};
    auto &&[arg, arg_errata]{parse_arg(name)};
    if (!arg_errata.is_ok()) {
      return std::move(arg_errata);
    }

    // Ignorable keys in the directive. Currently just one, so hand code it. Make this better
    // if there is ever more than one.
    if (name == Global::DO_KEY) {
      continue;
    }
    // See if this is in the factory. It's not an error if it's not, to enable adding extra
    // keys to directives. First key that is in the factory determines the directive type.
    // If none of the keys are in the factory, that's an error and is reported after the loop.
    if (auto spot{_factory.find(name)}; spot != _factory.end()) {
      auto &info = spot->second;
      auto rtti  = &_drtv_info[info._idx];

      if (!info._hook_mask[IndexFor(this->current_hook())]) {
        return Errata(S_ERROR, R"(Directive "{}" at {} is not allowed on hook "{}".)", name, drtv_node.Mark(),
                      this->current_hook());
      }

      // If this is the first use of the directive, do config level setup for the directive type.
      if (rtti->_count == 0) {
        info._cfg_init_cb(*this, rtti);
      }
      ++(rtti->_count);

      auto &&[drtv, drtv_errata]{info._load_cb(*this, rtti, drtv_node, name, arg, key_value)};
      if (!drtv_errata.is_ok()) {
        drtv_errata.note(R"(While parsing directive at {}.)", drtv_node.Mark());
        return std::move(drtv_errata);
      }
      drtv->_rtti = rtti;

      return std::move(drtv);
    }
  }
  return Errata(S_ERROR, R"(Directive at {} has no recognized tag.)", drtv_node.Mark());
}

Rv<Directive::Handle>
Config::parse_directive(YAML::Node const &drtv_node)
{
  if (drtv_node.IsMap()) {
    return this->load_directive(drtv_node);
  } else if (drtv_node.IsSequence()) {
    Errata zret;
    auto list{new DirectiveList};
    Directive::Handle drtv_list{list};
    for (auto child : drtv_node) {
      auto &&[handle, errata]{this->load_directive(child)};
      if (errata.is_ok()) {
        list->push_back(std::move(handle));
      } else {
        errata.note(R"(While loading directives at {}.)", drtv_node.Mark());
        return std::move(errata);
      }
    }
    return drtv_list;
  } else if (drtv_node.IsNull()) {
    return Directive::Handle(new NilDirective);
  }
  return Errata(S_ERROR, R"(Directive at {} is not an object or a sequence as required.)", drtv_node.Mark());
}

Errata
Config::load_top_level_directive(YAML::Node drtv_node)
{
  if (drtv_node.IsMap()) {
    YAML::Node key_node{drtv_node[When::KEY]};
    if (key_node) {
      auto &&[handle, errata]{When::load(*this, this->drtv_info(When::KEY), drtv_node, When::KEY, {}, key_node)};
      if (errata.is_ok()) {
        auto when = static_cast<When *>(handle.get());
        // Steal the directive out of the When.
        _roots[IndexFor(when->_hook)].emplace_back(std::move(when->_directive));
        if (Hook::POST_LOAD != _hook) { // post load hooks don't count.
          _has_top_level_directive_p = true;
        }
      } else {
        return std::move(errata);
      }
    } else {
      return Errata(S_ERROR, R"(Top level directive at {} is not a "when" directive as required.)", drtv_node.Mark());
    }
  } else {
    return Errata(S_ERROR, R"(Top level directive at {} is not an object as required.)", drtv_node.Mark());
  }
  return {};
}

Errata
Config::load_remap_directive(YAML::Node drtv_node)
{
  if (drtv_node.IsMap()) {
    auto &&[drtv, errata]{this->parse_directive(drtv_node)};
    if (errata.is_ok()) {
      // Don't unpack @c when - should not be that common and therefore better to defer them to the
      // context callbacks, avoids having to cart around the remap rule config to call them later.
      _roots[IndexFor(Hook::REMAP)].emplace_back(std::move(drtv));
      _has_top_level_directive_p = true;
    } else {
      return std::move(errata);
    }
  } else {
    return Errata(S_ERROR, R"(Configuration at {} is not a directive object as required.)", drtv_node.Mark());
  }
  return {};
}

Errata
Config::parse_yaml(YAML::Node root, TextView path)
{
  static constexpr TextView ROOT_PATH{"."};
  // Walk the key path and find the target. If the path is the special marker for ROOT_PATH
  // do not walk at all.
  for (auto p = (path == ROOT_PATH ? TextView{} : path); p;) {
    auto key{p.take_prefix_at('.')};
    if (auto node{root[key]}; node) {
      root.reset(node);
    } else {
      return Errata(S_ERROR, R"(Key "{}" not found - no such key "{}".)", path, path.prefix(path.size() - p.size()).rtrim('.'));
    }
  }

  Errata errata;

  // Special case remap loading.
  auto drtv_loader = &self_type::load_top_level_directive; // global loader.
  if (_hook == Hook::REMAP) {                              // loading only remap directives.
    drtv_loader = &self_type::load_remap_directive;
  }

  if (root.IsSequence()) {
    for (auto child : root) {
      errata.note((this->*drtv_loader)(child));
    }
    if (!errata.is_ok()) {
      errata.note(R"(While loading list of top level directives for "{}" at {}.)", path, root.Mark());
    }
  } else if (root.IsMap()) {
    errata = (this->*drtv_loader)(root);
  } else {
  }
  return errata;
};

Errata
Config::define(swoc::TextView name, HookMask const &hooks, Directive::InstanceLoader &&worker,
               Directive::CfgInitializer &&cfg_init_cb)
{
  auto &info{_factory[name]};
  info._idx         = _factory.size() - 1;
  info._hook_mask   = hooks;
  info._load_cb     = std::move(worker);
  info._cfg_init_cb = std::move(cfg_init_cb);
  return {};
}

Directive::CfgStaticData const *
Config::drtv_info(swoc::TextView const &name) const
{
  auto spot = _factory.find(name);
  return spot == _factory.end() ? nullptr : &_drtv_info[spot->second._idx];
}

/* ------------------------------------------------------------------------------------ */
Errata
Config::load_file(swoc::file::path const &cfg_path, TextView cfg_key, YamlCache *cache)
{
  if (auto spot = _cfg_files.find(cfg_path); spot != _cfg_files.end()) {
    if (spot->second.has_cfg_key(cfg_key)) {
      ts::DebugMsg(R"(Skipping "{}":{} - already loaded)", cfg_path, cfg_key);
      return {};
    } else {
      spot->second.add_cfg_key(cfg_key);
    }
  } else { // not found - put it in the table.
    auto [iter, flag] = _cfg_files.emplace(FileInfoMap::value_type{cfg_path, {}});
    iter->second.add_cfg_key(cfg_key);
  }

  YAML::Node root;
  // Try loading and parsing the file.
  if (cache) {
    if (auto cache_spot = cache->find(cfg_path); cache_spot != cache->end()) {
      root = cache_spot->second;
    }
  }

  if (root.IsNull()) {
    auto &&[yaml_node, yaml_errata]{yaml_load(cfg_path)};
    if (!yaml_errata.is_ok()) {
      yaml_errata.note(R"(While loading file "{}".)", cfg_path);
      return std::move(yaml_errata);
    }
    root = yaml_node;
    if (cache) {
      cache->emplace(cfg_path, root);
    }
  }

  // Process the YAML data.
  auto errata = this->parse_yaml(root, cfg_key);
  if (!errata.is_ok()) {
    errata.note(R"(While parsing key "{}" in configuration file "{}".)", cfg_key, cfg_path);
    return errata;
  }

  return {};
}
/* ------------------------------------------------------------------------------------ */
Errata
Config::load_file_glob(TextView pattern, swoc::TextView cfg_key, YamlCache *cache)
{
  int flags = 0;
  glob_t files;
  auto err_f                   = [](char const *, int) -> int { return 0; };
  swoc::file::path abs_pattern = ts::make_absolute(pattern);
  int result                   = glob(abs_pattern.c_str(), flags, err_f, &files);
  if (result == GLOB_NOMATCH) {
    return Errata(S_WARN, R"(The pattern "{}" did not match any files.)", abs_pattern);
  }
  for (size_t idx = 0; idx < files.gl_pathc; ++idx) {
    auto errata = this->load_file(swoc::file::path(files.gl_pathv[idx]), cfg_key, cache);
    if (!errata.is_ok()) {
      errata.note(R"(While processing pattern "{}".)", pattern);
      return errata;
    }
  }
  globfree(&files);
  return {};
}
/* ------------------------------------------------------------------------------------ */
Errata
Config::load_cli_args(Handle handle, const std::vector<std::string> &args, int arg_idx, YamlCache *cache)
{
  using argv_type = char const *; // Overall clearer in context of pointers to pointers.
  std::unique_ptr<argv_type[]> buff{new argv_type[args.size()]};
  swoc::MemSpan<argv_type> argv{buff.get(), args.size()};
  int idx = 0;
  for (auto const &arg : args) {
    argv[idx++] = arg.c_str();
  }
  return this->load_cli_args(handle, argv, arg_idx, cache);
}

Errata
Config::load_cli_args(Handle handle, swoc::MemSpan<char const *> argv, int arg_idx, YamlCache *cache)
{
  static constexpr TextView KEY_OPT    = "key";
  static constexpr TextView CONFIG_OPT = "config"; // An archaism for BC - take out someday.

  TextView cfg_key{_hook == Hook::REMAP ? REMAP_ROOT_KEY : GLOBAL_ROOT_KEY};
  for (unsigned idx = arg_idx; idx < argv.count(); ++idx) {
    TextView arg{std::string_view(argv[idx])};
    if (arg.empty()) {
      continue;
    }
    if (arg.front() == '-') {
      arg.ltrim('-');
      if (arg.empty()) {
        return Errata(S_ERROR, "Arg {} has an option prefix but no name.", idx);
      }

      TextView value;
      if (auto prefix = arg.prefix_at('='); !prefix.empty()) {
        value = arg.substr(prefix.size() + 1);
        arg   = prefix;
      } else if (++idx >= argv.count()) {
        return Errata(S_ERROR, "Arg {} is an option '{}' that requires a value but none was found.", idx, arg);
      } else {
        value = std::string_view{argv[idx]};
      }

      if (arg.starts_with_nocase(KEY_OPT)) {
        cfg_key = value;
      } else if (arg.starts_with_nocase(CONFIG_OPT)) {
        auto errata = this->load_file_glob(value, cfg_key, cache);
        if (!errata.is_ok()) {
          return errata;
        }
      } else {
        return Errata(S_ERROR, "Arg {} is an unrecognized option '{}'.", idx, arg);
      }
      continue;
    }
    auto errata = this->load_file_glob(arg, cfg_key, cache);
    if (!errata.is_ok()) {
      return errata;
    }
  }

  // Config loaded, run the post load directives and enable them to break the load by reporting
  // errors.
  auto &post_load_directives = this->hook_directives(Hook::POST_LOAD);
  if (post_load_directives.size() > 0) {
    std::unique_ptr<Context> ctx{new Context(handle)};
    for (auto &&drtv : post_load_directives) {
      auto errata = drtv->invoke(*ctx);
      if (!errata.is_ok()) {
        errata.note("While processing post-load directives.");
        return errata;
      }
    }
  }
  // Done with the files - clear them out.
  _cfg_file_count = _cfg_files.size();
  _cfg_files.clear();
  return {};
}

Config::ActiveFeatureScope
Config::feature_scope(ActiveType const &ex_type)
{
  ActiveFeatureScope scope(*this);
  _active_feature._ref_p = false;
  _active_feature._type  = ex_type;
  return scope;
}

Config::ActiveCaptureScope
Config::capture_scope(unsigned int count, unsigned int line_no)
{
  ActiveCaptureScope scope(*this);
  _active_capture._count = count;
  _active_capture._line  = line_no;
  _active_capture._ref_p = false;
  return scope;
}

Config::ActiveFeatureScope::~ActiveFeatureScope()
{
  if (_cfg) {
    _cfg->_active_feature = _state;
  }
}
