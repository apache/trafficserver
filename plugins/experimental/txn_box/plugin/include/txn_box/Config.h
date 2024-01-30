/** @file
 * Configuration classes.
 *
 * Copyright 2019, Oath Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <array>
#include <vector>
#if __has_include(<memory_resource>)
#include <memory_resource>
#endif

#include <swoc/TextView.h>
#include <swoc/MemArena.h>
#include <swoc/swoc_file.h>

#include "txn_box/common.h"
#include "txn_box/Extractor.h"
#include "txn_box/Expr.h"
#include "txn_box/FeatureGroup.h"
#include "txn_box/Directive.h"
#include "txn_box/yaml_util.h"

/// Contains a configuration and configuration helper methods.
/// This is also used to pass information between node parsing during configuration loading.
class Config
{
  using self_type = Config; ///< Self reference type.
  using Errata    = swoc::Errata;

public:
  /// Full name of the plugin.
  static constexpr swoc::TextView PLUGIN_NAME{"Transaction Tool Box"};
  /// Tag name of the plugin.
  static constexpr swoc::TextView PLUGIN_TAG{"txn_box"};

  static const std::string GLOBAL_ROOT_KEY; ///< Root key for global configuration.
  static const std::string REMAP_ROOT_KEY;  ///< Root key for remap configuration.

  /// Track the state of provided features.
  struct ActiveFeatureState {
    ActiveType _type;    ///< Type of active feature.
    bool _ref_p = false; ///< Feature has been referenced / used.
  };

  /// Scoped change to active feature.
  /// Caches the active feature on construction and restores it on destruction.
  class ActiveFeatureScope
  {
    using self_type = ActiveFeatureScope; ///< Self reference type.
    friend class Config;

    Config *_cfg = nullptr;    ///< Associated configuration instance.
    ActiveFeatureState _state; ///< Previous active feature,

  public:
    /** Construct from configuration.
     *
     * @param cfg Associated configuration.
     *
     * This caches the current active feature.
     */
    explicit ActiveFeatureScope(Config &cfg) : _cfg(&cfg), _state(cfg._active_feature) {}

    /// Move construcgtor.
    ActiveFeatureScope(self_type &&that) noexcept : _cfg(that._cfg), _state(that._state) { that._cfg = nullptr; }

    // No copying.
    ActiveFeatureScope(self_type const &that)   = delete;
    self_type &operator=(self_type const &that) = delete;

    ~ActiveFeatureScope();
  };

  friend ActiveFeatureScope;

  /** Create an active feature scope.
   *
   * @param ex_type Type of the updated active feature.
   * @return An RAII cache of the current active feature.
   */
  ActiveFeatureScope feature_scope(ActiveType const &ex_type);

  /// Track the state of the active capture groups.
  struct ActiveCaptureState {
    unsigned _count = 0;     ///< Number of active capture groups - 0 => not active.
    int _line       = -1;    ///< Line of the active regular expression.
    bool _ref_p     = false; ///< Regular expression capture groups referenced / used.
  };

  /// Scope for group capture.
  /// Caches the capture state on construction and restores it on destruction.
  class ActiveCaptureScope
  {
    using self_type = ActiveCaptureScope;
    friend class Config;

    Config *_cfg = nullptr;    ///< Associated config.
    ActiveCaptureState _state; ///< Cached capture state.

  public:
    /** Construct from configuration.
     *
     * @param cfg Associated configuration.
     */
    explicit ActiveCaptureScope(Config &cfg) : _cfg(&cfg), _state(cfg._active_capture) {}

    ActiveCaptureScope(self_type &&that) noexcept;

    // No copying.
    ActiveCaptureScope(self_type const &that)   = delete;
    self_type &operator=(self_type const &that) = delete;

    ~ActiveCaptureScope();
  };
  friend ActiveCaptureScope;

  /** Preserve the current capture group state.
   *
   * @param count Number of capture groups in the new state.
   * @param line_no Configuration line number that required the change.
   * @return A cached scope object.
   *
   * The current state is preserved in the returned object which, when destructed,
   * restores the previous state.
   */
  ActiveCaptureScope capture_scope(unsigned count, unsigned line_no);

  /** Local extractor table.
   *
   * Used for directive/modifier dependent extractors.
   *
   * Generally @c let should be used to restore the previous state.
   * @code
   * let ex_scope(cfg._local_extractors, local_table);
   * @endcode
   */
  Extractor::Table *_local_extractors = nullptr;

  /// Global and session variable map.
  using Variables = std::map<swoc::TextView, unsigned>;

  /// External handle to instances.
  using Handle = std::shared_ptr<self_type>;

  /// Cache of parsed YAML for files.
  /// @note Used only for remap.
  using YamlCache = std::unordered_map<swoc::file::path, YAML::Node>;

  /// Default constructor, makes an empty instance.
  Config();

  ~Config();

  /** Load the configuration from CLI arguments.
   *
   * @param handle The externally used handle to this instance.
   * @param args The CLI arguments.
   * @param arg_idx Index of the first CLI argument to be used.
   * @param cache Cache of the results of YAML parsing the files.
   * @return Errors, if any.
   */
  Errata load_cli_args(Handle handle, std::vector<std::string> const &args, int arg_idx = 0, YamlCache *cache = nullptr);

  /** Load the configuration from CLI arguments.
   *
   * @param handle The externally used handle to this instance.
   * @param args The CLI arguments.
   * @param arg_idx Index of the first CLI argument to be used.
   * @param cache Cache of the results of YAML parsing the files.
   * @return Errors, if any.
   */
  Errata load_cli_args(Handle handle, swoc::MemSpan<char const *> argv, int arg_idx = 0, YamlCache *cache = nullptr);

  /** Load file(s) in to @a this configuation.
   *
   * @param pattern File path pattern (standard glob format)
   * @param cfg_key Root key for configuration data.
   * @return Errors, if any.
   *
   * All files matching the @a pattern are loaded in to this configuration, using @a CfgKey as
   * the root key.
   */
  swoc::Errata load_file_glob(swoc::TextView pattern, swoc::TextView cfg_key, YamlCache *cache = nullptr);

  /** Load a file into @a this.
   *
   * @param cfg_path Path to configuration file.
   * @param cfg_key Root key in configuration.
   * @return Errors, if any.
   *
   * The content of @a cfg_path is loaded in to @a this configuration instance.
   */
  swoc::Errata load_file(swoc::file::path const &cfg_path, swoc::TextView cfg_key, YamlCache *cache = nullptr);

  /** Parse YAML from @a node to initialize @a this configuration.
   *
   * @param root Root node.
   * @param path Path from root node to the configuration based node.
   * @return Errors, if any.
   *
   * The @a path is an @c ARG_SEP separate list of keys. The value of the last key is the
   * node that is parsed. If the path is a single @c ARG_SEP the root node is parsed.
   *
   * @note Currently only @c Hook::REMAP is used for @a hook to handle the special needs of
   * a remap based configuration.
   *
   */
  Errata parse_yaml(YAML::Node root, swoc::TextView path);

  void
  mark_as_remap()
  {
    _hook = Hook::REMAP;
  }

  /** Load directives at the top level.
   *
   * @param node Base plugin configuation node.
   * @return Errors, if any.
   *
   * Processing of directives directly in the base node value is handled specially
   * by this method.
   */
  Errata load_top_level_directive(YAML::Node node);

  Errata load_remap_directive(YAML::Node node);

  /** Load / create a directive from a node.
   *
   * @param drtv_node Directive node.
   * @return A new directive instance, or errors if loading failed.
   */
  swoc::Rv<Directive::Handle> parse_directive(YAML::Node const &drtv_node);

  /** Parse a feature expression.
   *
   * @param fmt_node The node with the expression.
   * @return The expression or errors.
   *
   * This does extensive work for handle the various feature extraction capabilities. This should
   * be bypassed only in extreme cases where very specialized handling is needed. The result of
   * this can be passed to @c Context::extract to get the actual value at runtime.
   *
   * @see Context::extract
   */
  swoc::Rv<Expr> parse_expr(YAML::Node fmt_node);

#if __has_include(<memory_resource>) && _GLIBCXX_USE_CXX11_ABI
  /// Access the internal memory arena as a memory resource.
  std::pmr::memory_resource *
  pmr()
  {
    return &_arena;
  }
#endif

  enum LocalOpt {
    LOCAL_VIEW, ///< Localize as view.
    LOCAL_CSTR  ///< Localize as C string.
  };

  /** Copy @a text to local storage in this instance.
   *
   * @param text Text to copy.
   * @return The localized copy.
   *
   * Strings in the YAML configuration are transient. If the content needs to be available at
   * run time it must be first localized.
   */
  swoc::TextView &localize(swoc::TextView &text, LocalOpt opt = LOCAL_VIEW);
  swoc::TextView
  localize(std::string_view const &text, LocalOpt opt = LOCAL_VIEW)
  {
    swoc::TextView tv{text};
    return this->localize(tv, opt);
  }
  self_type &localize(Feature &feature);

  template <typename T>
  auto
  localize(T &) -> EnableForFeatureTypes<T, self_type &>
  {
    return *this;
  }

  /** Allocate config space for an array of @a T.
   *
   * @tparam T Element type.
   * @param count # of elements.
   * @return A span covering the allocated array.
   *
   * This allocates in the config storage. Constructors are not called. No destructors are called when the config is destructed. If
   * that is required use @c mark_for_cleanup
   *
   * @see mark_for_cleanup
   */
  template <typename T>
  swoc::MemSpan<T>
  alloc_span(unsigned count)
  {
    return _arena.alloc(sizeof(T) * count).rebind<T>();
  }

  /** Hook for which the directives are being loaded.
   *
   * @return The current hook.
   */
  Hook current_hook() const;

  /// @return The type of the active feature.
  ActiveType
  active_type() const
  {
    return _active_feature._type;
  }

  /** Require regular expression capture vectors to support at least @a n groups.
   *
   * @param n Number of capture groups.
   * @return @a this
   */
  self_type &require_rxp_group_count(unsigned n);

  /** Indicate a directive may be scheduled on a @a hook at runtime.
   *
   * @param hook Runtime dispatch hook.
   * @return @a this
   */
  self_type &
  reserve_slot(Hook hook)
  {
    ++_directive_count[IndexFor(hook)];
    return *this;
  }

  /// Check for top level directives.
  /// @return @a true if there are any top level directives, @c false if not.
  bool has_top_level_directive() const;

  /** Get the top level directives for a @a hook.
   *
   * @param hook The hook identifier.
   * @return A reference to the vector of top level directives for @a hook.
   */
  std::vector<Directive::Handle> const &hook_directives(Hook hook) const;

  /** Mark @a ptr for cleanup when @a this is destroyed.
   *
   * @tparam T Type of @a ptr
   * @param ptr Object to clean up.
   * @return @a this
   *
   * @a ptr is cleaned up by calling @c std::destroy_at.
   */
  template <typename T> self_type &mark_for_cleanup(T *ptr);

  /** Define a directive.
   *
   * @param name Directive name.
   * @param hooks Mask of valid hooks.
   * @param Options for the directive.
   * @param worker Functor to load / construct the directive from YAML.
   * @param cfg_init_cb Config time initialization if needed.
   * @return Errors, if any.
   */
  static swoc::Errata define(
    swoc::TextView name, HookMask const &hooks, Directive::InstanceLoader &&worker,
    Directive::CfgInitializer &&cfg_init_cb = [](Config &, Directive::CfgStaticData const *) -> swoc::Errata { return {}; });

  /** Define a directive.
   *
   * @tparam D The directive class.
   * @return Errors, if any.
   *
   * This is used when the directive class layout is completely standard. The template picks out those
   * pieces and passes them to the argument based @c define.
   */
  template <typename D> static swoc::Errata define();

  /** Define a directive alias.
   *
   * @tparam D The directive class.
   * @param name The alternative name
   * @return Errors, if any.
   *
   * This is used when a directive needs to be available under an alternative name. All of the arguments
   * are pulled from standard class members except the key (directive name).
   */
  template <typename D> static swoc::Errata define(swoc::TextView name);

  /** Allocate storage in @a this.
   *
   * @param n Size in bytes.
   * @param align Memory alignment.
   * @return The allocated memory.
   */
  swoc::MemSpan<void> allocate_cfg_storage(size_t n, size_t align = 1);

  /** Find or allocate an instance of @a T in configuration storage.
   *
   * @tparam T Type of object.
   * @tparam Args Arguments to @a T constructor.
   * @param name Name of object.
   * @return A pointer to an initialized instance of @a T in configuration storage.
   *
   * Looks for the named object and if found returns it. If the name is not found, a @a T is
   * allocated and constructed with the arguments after @a name forwarded.
   *
   * @note This should only be called during configuration loading.
   */
  template <typename T, typename... Args> T *obtain_named_object(swoc::TextView name, Args &&...args);

  /** Find named object.
   *
   * @tparam T Expected type of object.
   * @param name Name of the object.
   * @return A pointer to the object, or @c nullptr if not found.
   *
   * @note The caller is presumed to know that @a T is correct, no checks are done. This is purely a convenience to
   * avoid excessive casting.
   */
  template <typename T> T *named_object(swoc::TextView name);

  /** Prepare for context storage.
   *
   * @param n Number of bytes.
   * @return A reserved span that locates memory once the @c Context is created.
   *
   * This storage is not immediately allocated (in contrast to @c Context::span ). Instead it
   * is allocated when a @c Context is created. This is shared among instances of the directive in a
   * single transaction context, similarly to class static storage. This should be invoked during
   * directive type setup or object loading. Per instance context storage should be allocated during
   * invocation.
   *
   * Access to the storage is via @c Context::storage_for
   *
   * @see Context::storage_for
   */
  ReservedSpan reserve_ctx_storage(size_t n);

  /** Get the configuration level static information for a directive.
   *
   * @param name Name of the directive.
   * @return The directive info if found, @c nullptr if not.
   *
   * @note The directive itself should use its embedded pointer. This is required by other elements
   * that need access to shared configuration state for the directive.
   */
  Directive::CfgStaticData const *drtv_info(swoc::TextView const &name) const;

  /// @return Number of files loaded for this configuration.
  size_t
  file_count() const
  {
    return _cfg_file_count;
  }

  /// @return The total amount of context storage reserved.
  size_t
  reserved_ctx_storage_size() const
  {
    return _ctx_storage_required;
  }

  template <typename T>
  T *
  active_value(swoc::TextView const &name)
  {
    return static_cast<T *>(_active_values[name]);
  }

  struct active_value_save {
    void *&_value;
    void *_saved;

    active_value_save(void *&var, void *value) : _value(var), _saved(_value) { _value = value; }
    ~active_value_save() { _value = _saved; }
  };

  active_value_save
  active_value_let(swoc::TextView const &name, void *value)
  {
    return active_value_save(_active_values[name], value);
  }

protected:
  /// As the top level directive, this needs special access.
  friend class When;
  friend class Context;

  // Transient properties
  /// Current hook for directives being loaded.
  Hook _hook = Hook::INVALID;

  /// Mark whether there are any top level directives.
  bool _has_top_level_directive_p{false};

  /// Maximum number of capture groups for regular expression matching.
  /// Always at least one because literal matches use that.
  unsigned _capture_groups = 1;

  /** @defgroup Feature reference tracking.
   * A bit obscure but necessary because the active feature and the active capture groups must
   * be tracked independently because either can be overwritten independent of the other. When
   * directives are processed, a state instance can be passed to track back references. This is
   * checked and the pointers updated to point to that iff the incoming state marks the corresponding
   * tracking as active. These can therefore point to different states at different levels of
   * recursion, or the same. This allows the tracking to operate in a simple way, updating the data
   * for the specific tracking without having to do value checks.
   */
  /// @{
  using ActiveValues = std::unordered_map<swoc::TextView, void *, std::hash<std::string_view>>;
  /// Active (scoped) values used by elements (primarily directives and modifiers).
  /// Valid only during configuration load, not at run time.
  ActiveValues _active_values;
  swoc::MemArena _active_value_arena;

  ActiveFeatureState _active_feature; ///< Feature.
  ActiveCaptureState _active_capture; ///< Regular expression capture groups.
  /// #}

  /// Current amount of reserved config storage required.
  inline static size_t _cfg_storage_required = 0;

  /// Reserved configuration storage.
  swoc::MemSpan<void> _cfg_store;

  /// Current amount of shared context storage required.
  size_t _ctx_storage_required = 0;

  /// Array of config level information about directives in use.
  swoc::MemSpan<Directive::CfgStaticData> _drtv_info;

  /// A factory that maps from directive names to generator functions (@c Loader instances).
  using Factory = std::unordered_map<std::string_view, Directive::FactoryInfo>;

  /// The set of defined directives..
  static Factory _factory;

  /// Set of named configuration storage objects.
  std::unordered_map<swoc::TextView, swoc::MemSpan<void>, std::hash<std::string_view>> _named_objects;

  /// Top level directives for each hook. Always invoked.
  std::array<std::vector<Directive::Handle>, std::tuple_size<Hook>::value> _roots;

  /// Largest number of directives across the hooks. These are updated during
  /// directive load, if needed. This includes the top level directives.
  std::array<size_t, std::tuple_size<Hook>::value> _directive_count{0};

  /// For localizing data at a configuration level, primarily strings.
  swoc::MemArena _arena;

  /// Additional clean up to perform when @a this is destroyed.
  swoc::IntrusiveDList<Finalizer::Linkage> _finalizers;

  /** Load a directive.
   *
   * @param drtv_node Node containing the directive.
   * @return The directive.
   */
  swoc::Rv<Directive::Handle> load_directive(YAML::Node const &drtv_node);

  /** Parse a scalar feature expression.
   *
   * @param fmt_node The node with the extractor. Must be a scalar.
   * @return The expression or errors.
   *
   * Used for scalar expressions that are not NULL nor explicitly literal.
   *
   */
  swoc::Rv<Expr> parse_scalar_expr(YAML::Node node);

  /** Parse composite expression.
   *
   * @param text Input text.
   * @return An expression.
   *
   * "Composite" means (potentially) more than one extractor. If there is only a single extractor
   * the expression is reduced to immediate / direct expression as appropriate.
   */
  swoc::Rv<Expr> parse_composite_expr(swoc::TextView const &text);

  /** Parse an unquoted feature expression.
   *
   * @param text The unquoted text to parse. This must be non-empty.
   * @return The expression, or errors on failure.
   *
   */
  swoc::Rv<Expr> parse_unquoted_expr(swoc::TextView const &text);

  /** Parse an expression that contains modifiers.
   *
   * @param node Input node
   * @return The expression with attached modifiers.
   *
   * @a node is expected to the the list node that is the expression and modifiers.
   */
  swoc::Rv<Expr> parse_expr_with_mods(YAML::Node node);

  /** Update the (possible) extractor reference in @a spec.
   *
   * @param spec Specifier to update.
   * @return Value type of the specifier / extractor, or errors if invalid.
   *
   * @a spec is updated in place. If it is an extractor the extractor pointer in @a spec is updated.
   * This also validates the extractor can handle the @a spec details and enables config based
   * storage allocation if needed by the extractor.
   *
   * @see Extractor::validate
   */
  swoc::Rv<ActiveType> validate(Extractor::Spec &spec);

  /// Tracking for configuration files loaded in to @a this.
  class FileInfo
  {
    using self_type = FileInfo; ///< Self reference type.
  public:
    /** Check if a specific @a key has be used as a root for this file.
     *
     * @param key Root key name to check.
     * @return @c true if already used, @c false if not.
     */
    bool has_cfg_key(swoc::TextView key) const;

    /** Mark a root @a key as used.
     *
     * @param key Name of the key.
     */
    void add_cfg_key(swoc::TextView key);

  protected:
    std::list<std::string> _keys; ///< Root keys loaded from this file.
  };

  /// Mapping of absolute paths to @c FileInfo to track used configuration files / keys.
  using FileInfoMap = std::unordered_map<swoc::file::path, FileInfo>;
  /// Configuration file tracking map.
  FileInfoMap _cfg_files;
  /// # of configuration files tracked.
  /// Used for diagnostics.
  size_t _cfg_file_count = 0;
};

inline bool
Config::FileInfo::has_cfg_key(swoc::TextView key) const
{
  return _keys.end() != std::find_if(_keys.begin(), _keys.end(), [=](std::string const &k) { return 0 == strcasecmp(k, key); });
}

inline void
Config::FileInfo::add_cfg_key(swoc::TextView key)
{
  _keys.emplace_back(key);
}

inline Config::ActiveCaptureScope::ActiveCaptureScope(Config::ActiveCaptureScope::self_type &&that) noexcept
  : _cfg(that._cfg), _state(that._state)
{
  that._cfg = nullptr;
}

inline Config::ActiveCaptureScope::~ActiveCaptureScope()
{
  if (_cfg) {
    _cfg->_active_capture = _state;
  }
}

inline Hook
Config::current_hook() const
{
  return _hook;
}
inline bool
Config::has_top_level_directive() const
{
  return _has_top_level_directive_p;
}

inline std::vector<Directive::Handle> const &
Config::hook_directives(Hook hook) const
{
  return _roots[static_cast<unsigned>(hook)];
}

inline Config &
Config::require_rxp_group_count(unsigned n)
{
  _capture_groups = std::max(_capture_groups, n);
  return *this;
}

template <typename T>
auto
Config::mark_for_cleanup(T *ptr) -> self_type &
{
  auto f = _arena.make<Finalizer>(ptr, [](void *ptr) { std::destroy_at(static_cast<T *>(ptr)); });
  _finalizers.append(f);
  return *this;
}

template <typename D>
swoc::Errata
Config::define()
{
  return self_type::define(D::KEY, D::HOOKS, &D::load, &D::cfg_init);
}

template <typename D>
swoc::Errata
Config::define(swoc::TextView name)
{
  return self_type::define(name, D::HOOKS, &D::load, &D::cfg_init);
}

template <typename T, typename... Args>
T *
Config::obtain_named_object(swoc::TextView name, Args &&...args)
{
  auto spot = _named_objects.find(name);
  if (spot != _named_objects.end()) {
    return spot->second.rebind<T>().data();
  }
  auto span            = this->allocate_cfg_storage(sizeof(T), alignof(T));
  _named_objects[name] = span;
  return new (span.data()) T(std::forward<Args>(args)...);
}

template <typename T>
T *
Config::named_object(swoc::TextView name)
{
  auto spot = _named_objects.find(name);
  return spot != _named_objects.end() ? spot->second.rebind<T>().data() : nullptr;
}
