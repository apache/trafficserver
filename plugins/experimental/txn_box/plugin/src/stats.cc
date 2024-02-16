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

#include "txn_box/common.h"

#include <string>

#include <swoc/TextView.h>
#include <swoc/Errata.h>
#include <swoc/BufferWriter.h>

#include "txn_box/Config.h"
#include "txn_box/Directive.h"
#include "txn_box/Context.h"

#include "txn_box/ts_util.h"

using swoc::TextView;
using swoc::Errata;
using swoc::Rv;
using swoc::BufferWriter;
using namespace swoc::literals;

/* ------------------------------------------------------------------------------------ */
/// Define a plugin statistic.
class Do_stat_define : public Directive
{
  using self_type  = Do_stat_define; ///< Self reference type.
  using super_type = Directive;      ///< Parent type.
  friend struct Stat;

protected:
  struct CfgInfo;

public:
  static inline const std::string KEY{"stat-define"}; ///< Directive name.
  static const HookMask HOOKS;                        ///< Valid hooks for directive.

  Errata invoke(Context &ctx) override; ///< Runtime activation.

  /** Load from YAML node.
   *
   * @param cfg Configuration data.
   * @param rtti Configuration level static data for this directive.
   * @param drtv_node Node containing the directive.
   * @param name Name from key node tag.
   * @param arg Arg from key node tag.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config &cfg, CfgStaticData const *rtti, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

  static Errata cfg_init(Config &cfg, CfgStaticData const *rtti);

protected:
  /// Mapping internal names to full names.
  using Names = std::unordered_map<TextView, TextView, std::hash<std::string_view>>;
  /// Data in reserved configuration storage.
  struct CfgInfo {
    Names _names; ///< Map of internal names to full names.
  };

  /** Get the full stat name.
   *
   * @param cfg Configuration instance.
   * @param name Stat name in configuration.
   * @return The full name of the stat if defined, or a localized copy if not.
   */
  static TextView expand_and_localize(Config &cfg, TextView const &name);

  TextView _name;             ///< Stat name (internal)
  TextView _full_name;        ///< Fullname including prefix.
  int _value         = 0;     ///< Initial value.
  bool _persistent_p = false; ///< Make persistent.

  static inline const std::string NAME_TAG{"name"};
  static inline const std::string VALUE_TAG{"value"};
  static inline const std::string PERSISTENT_TAG{"persistent"};
  static inline const std::string PREFIX_TAG{"prefix"};
};

const HookMask Do_stat_define::HOOKS{MaskFor(Hook::POST_LOAD)};

TextView
Do_stat_define::expand_and_localize(Config &cfg, TextView const &name)
{
  if (auto cfg_info = cfg.named_object<CfgInfo>(KEY); cfg_info) {
    if (auto spot = cfg_info->_names.find(name); spot != cfg_info->_names.end()) {
      return spot->second;
    }
  }
  return cfg.localize(name);
}

Errata
Do_stat_define::cfg_init(Config &cfg, CfgStaticData const *)
{
  auto cfg_info = cfg.obtain_named_object<CfgInfo>(KEY);
  // Clean it up when the config is destroyed.
  cfg.mark_for_cleanup(cfg_info);
  return {};
}

Errata
Do_stat_define::invoke(Context &)
{
  auto &&[idx, errata]{ts::plugin_stat_define(_full_name, _value, _persistent_p)};
  return std::move(errata);
}

Rv<Directive::Handle>
Do_stat_define::load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &, swoc::TextView const &,
                     YAML::Node key_value)
{
  auto self = new self_type();
  Handle handle(self);

  // Prefix is optional - defaults to "plugin.txn_box"
  // Need this before the name so it can be combined as needed.
  auto prefix_node = key_value[PREFIX_TAG];
  TextView prefix;
  if (prefix_node) {
    auto &&[prefix_expr, prefix_errata]{cfg.parse_expr(prefix_node)};
    if (!prefix_errata.is_ok()) {
      prefix_errata.note("While parsing {} directive at {}.", KEY, drtv_node.Mark());
      return std::move(prefix_errata);
    }
    if (!prefix_expr.is_literal() || !prefix_expr.result_type().can_satisfy(STRING)) {
      return Errata(S_ERROR, "{} value at {} for {} directive at {} must be a literal string.", PREFIX_TAG, prefix_node.Mark(), KEY,
                    drtv_node.Mark());
    }

    prefix = std::get<IndexFor(STRING)>(std::get<Expr::LITERAL>(prefix_expr._raw));
    drtv_node.remove(prefix_node);
  } else {
    prefix = "plugin.txn_box"_tv; // default if not explicitly set.
  }
  // Note - prefix isn't localized yet, need to wait for name.

  auto name_node = key_value[NAME_TAG];
  if (!name_node) {
    return Errata(S_ERROR, "{} directive at {} must have a {} key.", KEY, drtv_node.Mark(), NAME_TAG);
  }

  auto &&[name_expr, name_errata]{cfg.parse_expr(name_node)};
  if (!name_errata.is_ok()) {
    name_errata.note("While parsing {} directive at {}.", KEY, drtv_node.Mark());
    return std::move(name_errata);
  }
  if (!name_expr.is_literal() || !name_expr.result_type().can_satisfy(STRING)) {
    return Errata(S_ERROR, "{} value at {} for {} directive at {} must be a literal string.", NAME_TAG, name_node.Mark(), KEY,
                  drtv_node.Mark());
  }
  TextView name = std::get<IndexFor(STRING)>(std::get<Expr::LITERAL>(name_expr._raw));
  if (name.empty()) {
    return Errata(S_ERROR, "{} value at {} for {} directive at {} must be a non-empty literal string.", NAME_TAG, name_node.Mark(),
                  KEY, drtv_node.Mark());
  }

  // Localize prefix and name in the same string, storing views in to that string.
  if (prefix.empty()) {
    self->_full_name = self->_name = cfg.localize(name, Config::LOCAL_CSTR);
  } else {
    swoc::FixedBufferWriter w{cfg.allocate_cfg_storage(prefix.size() + 1 + name.size() + 1)};
    w.write(prefix).write('.').write(name).write('\0');
    self->_full_name = TextView{w.view()}.remove_suffix(1); // drop terminal null.
    self->_name      = self->_full_name.suffix(name.size());
  }
  Names &names = cfg.named_object<CfgInfo>(KEY)->_names;
  names.insert({self->_name, self->_full_name});
  drtv_node.remove(name_node);

  auto value_node = key_value[VALUE_TAG];
  if (value_node) {
    auto &&[value_expr, value_errata]{cfg.parse_expr(value_node)};
    if (!value_errata.is_ok()) {
      value_errata.note("While parsing {} directive at {}.", KEY, drtv_node.Mark());
      return std::move(value_errata);
    }
    if (!value_expr.is_literal() || !value_expr.result_type().can_satisfy(INTEGER)) {
      return Errata(S_ERROR, "{} value at {} for {} directive at {} must be a literal integer.", VALUE_TAG, value_node.Mark(), KEY,
                    drtv_node.Mark());
    }
    drtv_node.remove(value_node);
    self->_value = std::get<IndexFor(INTEGER)>(std::get<Expr::LITERAL>(value_expr._raw));
  }

  auto persistent_node = key_value[PERSISTENT_TAG];
  if (persistent_node) {
    auto &&[persistent_expr, persistent_errata]{cfg.parse_expr(persistent_node)};
    if (!persistent_errata.is_ok()) {
      persistent_errata.note("While parsing {} directive at {}.", KEY, drtv_node.Mark());
      return std::move(persistent_errata);
    }
    if (!persistent_expr.is_literal() || !persistent_expr.result_type().can_satisfy(BOOLEAN)) {
      return Errata(S_ERROR, "{} value at {} for {} directive at {} must be a literal string.", PERSISTENT_TAG,
                    persistent_node.Mark(), KEY, drtv_node.Mark());
    }
    drtv_node.remove(persistent_node); // ugly, need to fix the overall API.
    self->_persistent_p = std::get<IndexFor(BOOLEAN)>(std::get<Expr::LITERAL>(persistent_expr._raw));
  }
  return handle;
}
/* ------------------------------------------------------------------------------------ */
/// Statistic information.
/// The name is used when it can't be resolved during configuration loading.
struct Stat {
  static constexpr int UNRESOLVED = -1;
  static constexpr int INVALID    = -2;
  TextView _name;        ///< Statistic name.
  int _idx = UNRESOLVED; ///< Statistic index.

  Stat(Config &cfg, TextView const &name) { this->assign(cfg, name); }

  Stat &
  assign(Config &cfg, TextView name)
  {
    _name = Do_stat_define::expand_and_localize(cfg, name);

    _idx = ts::plugin_stat_index(_name);
    _idx = _idx < 0 ? UNRESOLVED : _idx; // normalize.
    return *this;
  }

  int
  index()
  {
    if (_idx == UNRESOLVED) {
      _idx = ts::plugin_stat_index(_name);
      if (_idx < 0) { // On a lookup failure, give up and prevent future lookups.
        _idx = INVALID;
      }
    }
    return _idx;
  }

  Feature
  value()
  {
    auto n{this->index()};
    return n < 0 ? NIL_FEATURE : feature_type_for<INTEGER>{ts::plugin_stat_value(_idx)};
  }

  Stat &
  update(feature_type_for<INTEGER> value)
  {
    auto n{this->index()};
    if (n >= 0) {
      ts::plugin_stat_update(n, value);
    }
    return *this;
  }
};
/* ------------------------------------------------------------------------------------ */
class Do_stat_update : public Directive
{
  using self_type  = Do_stat_update; ///< Self reference type.
  using super_type = Directive;      ///< Parent type.
public:
  static const std::string KEY; ///< Directive name.
  static const HookMask HOOKS;  ///< Valid hooks for directive.

  Errata invoke(Context &ctx) override; ///< Runtime activation.

  /** Load from YAML node.
   *
   * @param cfg Configuration data.
   * @param rtti Configuration level static data for this directive.
   * @param drtv_node Node containing the directive.
   * @param name Name from key node tag.
   * @param arg Arg from key node tag.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config &cfg, CfgStaticData const *rtti, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

protected:
  Stat _stat; ///< Stat to update.
  Expr _expr; ///< Value of update.

  Do_stat_update(Config &cfg, TextView const &name, Expr &&expr);
};

const std::string Do_stat_update::KEY{"stat-update"};
const HookMask Do_stat_update::HOOKS{MaskFor({Hook::CREQ, Hook::PREQ, Hook::PRE_REMAP, Hook::REMAP, Hook::POST_REMAP, Hook::PRSP,
                                              Hook::URSP, Hook::TXN_START, Hook::TXN_CLOSE})};

Do_stat_update::Do_stat_update(Config &cfg, TextView const &name, Expr &&expr) : _stat{cfg, name}, _expr(std::move(expr)) {}

Errata
Do_stat_update::invoke(Context &ctx)
{
  auto [value, errata]{ctx.extract(_expr).as_integer(0)};
  if (value != 0) {
    _stat.update(value);
  }
  return std::move(errata);
}

Rv<Directive::Handle>
Do_stat_update::load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &, swoc::TextView const &arg,
                     YAML::Node key_value)
{
  if (key_value.IsNull()) {
    return Handle(new self_type(cfg, arg, Expr{feature_type_for<INTEGER>(1)}));
  }

  auto &&[expr, errata]{cfg.parse_expr(key_value)};
  if (!errata.is_ok()) {
    return std::move(errata);
  }

  if (!expr.result_type().can_satisfy(INTEGER)) {
    return Errata(S_ERROR, "Value for {} directive at {} must be an integer.", KEY, drtv_node.Mark());
  }

  return Handle(new self_type{cfg, arg, std::move(expr)});
}
/* ------------------------------------------------------------------------------------ */
class Ex_stat : public Extractor
{
  using self_type  = Ex_stat;   ///< Self reference type.
  using super_type = Extractor; ///< Parent type.
public:
  static constexpr TextView NAME{"stat"};

  Rv<ActiveType> validate(Config &cfg, Spec &spec, TextView const &arg) override;
  using Extractor::extract; // un-hide the overloaded
  Feature extract(Context &ctx, Spec const &spec) override;
  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;
};

Rv<ActiveType>
Ex_stat::validate(Config &cfg, Spec &spec, const TextView &arg)
{
  if (arg.empty()) {
    return Errata(S_ERROR, R"("{}" extractor requires an argument to specify the statistic.)", NAME);
  }
  spec._data.span = cfg.alloc_span<Stat>(1); // allocate and stash.
  spec._data.span.rebind<Stat>()[0].assign(cfg, arg);
  return {INTEGER};
}

Feature
Ex_stat::extract(Context &, const Spec &spec)
{
  return spec._data.span.rebind<Stat>()[0].value();
}

BufferWriter &
Ex_stat::format(BufferWriter &w, Spec const &spec, Context &ctx)
{
  return bwformat(w, spec, this->extract(ctx, spec));
}

/* ------------------------------------------------------------------------------------ */

namespace
{
Ex_stat stat;

[[maybe_unused]] bool INITIALIZED = []() -> bool {
  Config::define<Do_stat_define>();
  Config::define<Do_stat_update>();
  Extractor::define(Ex_stat::NAME, &stat);
  return true;
}();
} // namespace
