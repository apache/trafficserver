/** @file
   Text Block directives and extractors.

 * Copyright 2020 Verizon Media
 * SPDX-License-Identifier: Apache-2.0
*/

#include <shared_mutex>

#include "txn_box/common.h"

#include <swoc/TextView.h>
#include <swoc/Errata.h>
#include <swoc/ArenaWriter.h>
#include <swoc/BufferWriter.h>
#include <swoc/bwf_base.h>
#include <swoc/bwf_ex.h>
#include <swoc/bwf_std.h>

#include "txn_box/Directive.h"
#include "txn_box/Extractor.h"
#include "txn_box/Comparison.h"
#include "txn_box/Config.h"
#include "txn_box/Context.h"

#include "txn_box/yaml_util.h"
#include "txn_box/ts_util.h"

using swoc::TextView;
using swoc::Errata;
using swoc::Rv;
using swoc::BufferWriter;
namespace bwf = swoc::bwf;
using namespace swoc::literals;
using Clock = std::chrono::system_clock;

/* ------------------------------------------------------------------------------------ */

/** Define a static text block.
 *
 * The content is stored in s shared pointer to a @c std::string.
 *
 * The shared pointer is used so the content can be persisted during a transaction even if there is a reload of that content.
 *
 * @c std::string is used because reloads make the content lifetime asynchronous with both configuration and transactions, making
 * those arenas not appropriate.
 */
class Do_text_block_define : public Directive
{
  using self_type  = Do_text_block_define; ///< Self reference type.
  using super_type = Directive;            ///< Parent type.
protected:
  struct CfgInfo;

public:
  static inline const std::string KEY{"text-block-define"}; ///< Directive name.
  static const HookMask HOOKS;                              ///< Valid hooks for directive.

  /// Functor to do file content updating as needed.
  struct Updater {
    std::weak_ptr<Config> _cfg;   ///< Configuration.
    Do_text_block_define *_block; ///< Text block holder.

    void operator()(); ///< Do the update check.
  };

  ~Do_text_block_define() noexcept;

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

  /** Create config level shared data.
   *
   * @param cfg Configuration.
   * @param rtti Static configuration data
   * @return
   */
  static Errata cfg_init(Config &cfg, CfgStaticData const *rtti);

protected:
  /// Storage for instances.
  using Map       = std::unordered_map<TextView, self_type *, std::hash<std::string_view>>;
  using MapHandle = std::unique_ptr<Map>;

  /// Config level data for all text blocks.
  struct CfgInfo {
    MapHandle _map; ///< Map of names to specific text block definitions.

    explicit CfgInfo(MapHandle &&map) : _map(std::move(map)) {}
  };

  TextView _name;                                                             ///< Block name.
  swoc::file::path _path;                                                     ///< Path to file (optional)
  std::optional<TextView> _text;                                              ///< Default literal text (optional)
  feature_type_for<DURATION> _duration;                                       ///< Time between update checks.
  std::atomic<Clock::duration> _last_check = Clock::now().time_since_epoch(); ///< Absolute time of the last alert.
  Clock::time_point _last_modified;                                           ///< Last modified time of the file.
  std::shared_ptr<std::string> _content;                                      ///< Content of the file.
  int _line_no = 0;                                                           ///< For debugging name conflicts.
  std::shared_mutex _content_mutex;                                           ///< Lock for access @a content.
  ts::TaskHandle _task;                                                       ///< Handle for periodic checking task.

  FeatureGroup _fg; ///< Support cross reference in the keys.
  using index_type                  = FeatureGroup::index_type;
  static auto constexpr INVALID_IDX = FeatureGroup::INVALID_IDX;
  index_type _notify_idx; ///< FG index for notifications.

  static inline const std::string NAME_TAG{"name"};
  static inline const std::string PATH_TAG{"path"};
  static inline const std::string TEXT_TAG{"text"};
  static inline const std::string DURATION_TAG{"duration"};
  static inline const std::string NOTIFY_TAG{"notify"};

  /// Map of names to text blocks.
  static Map *map(Config &cfg);

  /// Get the "update" time for a file - the max of modified and changed times.
  static Clock::time_point update_time(swoc::file::file_status const &stat);

  /// Default constructor - only available to friends.
  Do_text_block_define() = default;

  friend class Ex_text_block;
  friend class Mod_as_text_block;
  friend Updater;
};

const HookMask Do_text_block_define::HOOKS{MaskFor(Hook::POST_LOAD)};

inline Clock::time_point
Do_text_block_define::update_time(swoc::file::file_status const &stat)
{
  return std::max(swoc::file::last_write_time(stat), swoc::file::status_time(stat));
}

Do_text_block_define::~Do_text_block_define() noexcept
{
  _task.cancel();
}

auto
Do_text_block_define::map(Config &cfg) -> Map *
{
  auto cfg_info = cfg.named_object<CfgInfo>(KEY);
  return cfg_info ? cfg_info->_map.get() : nullptr;
}

Errata
Do_text_block_define::invoke(Context &ctx)
{
  // Set up the update checking.
  if (_duration.count()) {
    _task =
      ts::PerformAsTaskEvery(Updater{ctx.acquire_cfg(), this}, std::chrono::duration_cast<std::chrono::milliseconds>(_duration));
  }
  return {};
}

Rv<Directive::Handle>
Do_text_block_define::load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &, swoc::TextView const &,
                           YAML::Node key_value)
{
  auto self = new self_type();
  Handle handle(self);
  auto &fg       = self->_fg;
  self->_line_no = drtv_node.Mark().line;

  auto errata = self->_fg.load(cfg, key_value,
                               {
                                 {NAME_TAG, FeatureGroup::REQUIRED},
                                 {PATH_TAG},
                                 {TEXT_TAG},
                                 {DURATION_TAG},
                                 {NOTIFY_TAG}
  });

  if (!errata.is_ok()) {
    errata.note(R"(While parsing value at {} in "{}" directive at {}.)", key_value.Mark(), KEY, drtv_node.Mark());
    return errata;
  }
  auto idx = fg.index_of(NAME_TAG);

  // Must have a NAME, and either TEXT or PATH. DURATION is optional, but must be a duration if present.
  auto &name_expr{fg[idx]._expr};
  if (!name_expr.is_literal() || !name_expr.result_type().can_satisfy(STRING)) {
    return Errata(S_ERROR, "{} value for {} directive at {} must be a literal string.", NAME_TAG, KEY, drtv_node.Mark());
  }
  self->_name = std::get<IndexFor(STRING)>(std::get<Expr::LITERAL>(name_expr._raw));

  if (auto path_idx = fg.index_of(PATH_TAG); path_idx != INVALID_IDX) {
    auto &path_expr = fg[path_idx]._expr;
    if (!path_expr.is_literal() || !path_expr.result_type().can_satisfy(STRING)) {
      return Errata(S_ERROR, "{} value for {} directive at {} must be a literal string.", PATH_TAG, KEY, drtv_node.Mark());
    }
    self->_path = cfg.localize(ts::make_absolute(std::get<IndexFor(STRING)>(std::get<Expr::LITERAL>(path_expr._raw))).view().data(),
                               Config::LOCAL_CSTR);
  }

  if (auto text_idx = fg.index_of(TEXT_TAG); text_idx != INVALID_IDX) {
    auto &text_expr = fg[text_idx]._expr;
    if (!text_expr.is_literal() || !text_expr.result_type().can_satisfy(STRING)) {
      return Errata(S_ERROR, "{} value for {} directive at {} must be a literal string.", TEXT_TAG, KEY, drtv_node.Mark());
    }
    self->_text = std::get<IndexFor(STRING)>(std::get<Expr::LITERAL>(text_expr._raw));
  }

  if (!self->_text.has_value() && self->_path.empty()) {
    return Errata(S_ERROR, "{} directive at {} must have a {} or a {} key.", KEY, drtv_node.Mark(), PATH_TAG, TEXT_TAG);
  }

  if (auto dur_idx = fg.index_of(DURATION_TAG); dur_idx != INVALID_IDX) {
    auto &dur_expr = fg[dur_idx]._expr;
    if (!dur_expr.is_literal()) {
      return Errata(S_ERROR, "{} value for {} directive at {} must be a literal duration.", DURATION_TAG, KEY, drtv_node.Mark());
    }
    auto &&[dur_value, dur_value_errata]{std::get<Expr::LITERAL>(dur_expr._raw).as_duration()};
    if (!dur_value_errata.is_ok()) {
      return Errata(S_ERROR, "{} value for {} directive at {} is not a valid duration.", DURATION_TAG, KEY, drtv_node.Mark());
    }
    self->_duration = dur_value;
  }

  self->_notify_idx = fg.index_of(NOTIFY_TAG);

  if (!self->_path.empty()) {
    std::error_code ec;
    auto content = swoc::file::load(self->_path, ec);
    if (!ec) {
      self->_content = std::make_shared<std::string>(std::move(content));
    } else if (self->_text.has_value()) {
      self->_content = nullptr;
    } else {
      return Errata(S_ERROR,
                    R"("{}" directive at {} - value "{}" for key "{}" is not readable [{}] and no alternate "{}" key was present.)",
                    KEY, drtv_node.Mark(), self->_path, PATH_TAG, ec, TEXT_TAG);
    }
    self->_last_modified = self_type::update_time(swoc::file::status(self->_path, ec));
  }

  // Put the directive in the map.
  Map *map = self->map(cfg);
  if (auto spot = map->find(self->_name); spot != map->end()) {
    return Errata(S_ERROR, R"("{}" directive at {} has the same name "{}" as another instance at line {}.)", KEY, drtv_node.Mark(),
                  self->_name, spot->second->_line_no);
  }
  (*map)[self->_name] = self;

  return handle;
}

Errata
Do_text_block_define::cfg_init(Config &cfg, CfgStaticData const *)
{
  auto cfg_info = cfg.obtain_named_object<CfgInfo>(KEY, MapHandle(new Map));
  cfg.mark_for_cleanup(cfg_info);
  return {};
}

void
Do_text_block_define::Updater::operator()()
{
  auto cfg = _cfg.lock(); // Make sure the config is still around while work is done.
  if (!cfg) {
    return; // presume the config destruction is ongoing and will clean this up.
  }

  // This should be scheduled at the appropriate intervals and so no need to check time.
  std::error_code ec;
  auto fs = swoc::file::status(_block->_path, ec);
  if (!ec) {
    auto mtime = self_type::update_time(fs);
    if (mtime <= _block->_last_modified) {
      return; // same as it ever was...
    }
    auto content = std::make_shared<std::string>();
    *content     = swoc::file::load(_block->_path, ec);
    if (!ec) { // swap in updated content.
      {
        std::unique_lock lock(_block->_content_mutex);
        _block->_content       = content;
        _block->_last_modified = mtime;
      }
      if (_block->_notify_idx != FeatureGroup::INVALID_IDX) {
        Context ctx(cfg);
        auto text{_block->_fg.extract(ctx, _block->_notify_idx)};
        auto msg = ctx.render_transient([&](BufferWriter &w) { w.print("[{}] {}", Config::PLUGIN_TAG, text); });
        ts::Log_Note(msg);
      }
      return;
    }
  }
  // If control flow gets here, the file is no longer accessible and the content
  // should be cleared. If the file shows up again, it should have a modified time
  // later than the previously existing file, so that can be left unchanged.
  std::unique_lock lock(_block->_content_mutex);
  _block->_content.reset();
}

/* ------------------------------------------------------------------------------------ */
/// Text block extractor.
class Ex_text_block : public Extractor
{
  using self_type  = Ex_text_block; ///< Self reference type.
  using super_type = Extractor;     ///< Parent type.
public:
  static constexpr TextView NAME{"text-block"};

  Rv<ActiveType> validate(Config &cfg, Spec &spec, TextView const &arg) override;

  Feature extract(Context &ctx, Spec const &spec) override;
  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;

protected:
  /** Extract the content of the block for @a tag.
   *
   * @param ctx Transaction context.
   * @param tag Block tag.
   * @return A feature, a @c STRING if there is block content, @c NIL if not.
   *
   * The view returned is transaction persistent.
   */
  static Feature extract_block(Context &ctx, TextView tag);

  friend class Mod_as_text_block; // Access to @c extract_block.
};

Rv<ActiveType>
Ex_text_block::validate(Config &cfg, Spec &spec, const TextView &arg)
{
  if (arg.empty()) {
    return Errata(S_ERROR, R"("{}" extractor requires an argument to specify the defined text block.)", NAME);
  }
  auto view       = cfg.alloc_span<TextView>(1);
  view[0]         = cfg.localize(TextView{arg});
  spec._data.span = view.rebind<void>();
  return {STRING};
}

Feature
Ex_text_block::extract_block(Context &ctx, TextView tag)
{
  if (auto info = ctx.cfg().named_object<Do_text_block_define::CfgInfo>(Do_text_block_define::KEY); info) {
    if (auto spot = info->_map->find(tag); spot != info->_map->end()) {
      auto block = spot->second;
      std::shared_ptr<std::string> content;
      { // grab a copy of the shared pointer to file content.
        std::shared_lock lock(block->_content_mutex);
        content = block->_content;
      }

      if (content) {
        // If there is content, it must persist the content until end of the directive. There's no direct support for that so the
        // best that can be done is to persist until the end of the transaction by putting it in context storage.
        auto *ptr = ctx.make<std::shared_ptr<std::string>>(content); // make a copy in context storage.
        ctx.mark_for_cleanup(ptr);                                   // clean it up when the txn is done.
        return FeatureView(*content);
      }

      // No file content, see if there's alternate text.
      if (block->_text.has_value()) {
        return FeatureView{block->_text.value()};
      }
    }
  }
  return NIL_FEATURE;
}

Feature
Ex_text_block::extract(Context &ctx, const Spec &spec)
{
  auto arg = spec._data.span.rebind<TextView>()[0];
  return this->extract_block(ctx, arg);
}

BufferWriter &
Ex_text_block::format(BufferWriter &w, Spec const &spec, Context &ctx)
{
  return bwformat(w, spec, this->extract(ctx, spec));
}

// ------------------
/// Convert to a text block, treating the active value as a text block name.
class Mod_as_text_block : public Modifier
{
  using self_type = Mod_as_text_block;
  ;
  using super_type = Modifier;

public:
  static constexpr swoc::TextView KEY{"as-text-block"};

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

  /// Resulting type of feature after modifying always a string.
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

  explicit Mod_as_text_block(Expr &&expr) : _value(std::move(expr)) {}
};

bool
Mod_as_text_block::is_valid_for(ActiveType const &ex_type) const
{
  return ex_type.can_satisfy({NIL, STRING});
}

ActiveType
Mod_as_text_block::result_type(ActiveType const &) const
{
  return {MaskFor(STRING)};
}

Rv<Modifier::Handle>
Mod_as_text_block::load(Config &cfg, YAML::Node, TextView, TextView, YAML::Node key_value)
{
  auto &&[expr, errata]{cfg.parse_expr(key_value)};
  if (!errata.is_ok()) {
    errata.note(R"(While parsing "{}" modifier at {}.)", KEY, key_value.Mark());
    return std::move(errata);
  }
  if (expr.is_null()) { // If no default, use an empty string.
    return Handle(new self_type(Expr(FeatureView::Literal(""))));
  } else if (expr.result_type().can_satisfy(MaskFor(STRING))) {
    return Handle(new self_type{std::move(expr)});
  }
  return Errata(S_ERROR, "Value of {} modifier is not of type {}.", KEY, STRING);
}

Rv<Feature>
Mod_as_text_block::operator()(Context &ctx, Feature &feature)
{
  Feature zret;
  if (IndexFor(STRING) == feature.index()) {
    auto tag = std::get<IndexFor(STRING)>(feature); // get the name.
    zret     = Ex_text_block::extract_block(ctx, tag);
  }

  if (is_nil(zret)) {
    zret = ctx.extract_view(_value, {Context::ViewOption::EX_COMMIT});
  }

  return {zret};
}

/* ------------------------------------------------------------------------------------ */

namespace
{
Ex_text_block text_block;

[[maybe_unused]] bool INITIALIZED = []() -> bool {
  Config::define<Do_text_block_define>();
  Extractor::define(Ex_text_block::NAME, &text_block);
  Modifier::define<Mod_as_text_block>();
  return true;
}();
} // namespace
