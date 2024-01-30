/** @file
   Per transaction context types.

 * Copyright 2019, Oath Inc.
 * SPDX-License-Identifier: Apache-2.0
*/

#pragma once

#include <memory>
#if __has_include(<memory_resource>)
#include <memory_resource>
#endif
#include <functional>

#include <swoc/MemArena.h>
#include <swoc/Errata.h>
#include <swoc/IntrusiveHashMap.h>

#include "txn_box/common.h"
#include "txn_box/Rxp.h"
#include "txn_box/Extractor.h"
#include "txn_box/Expr.h"
#include "txn_box/ts_util.h"
#include <ts/remap.h>

class Directive;

/** Per transaction context.
 *
 * This holds data associated with a specific transaction, along with pointers / references to
 * global structures, such that a transaction based hook can retrieve all necessary information
 * from a point to an instance of this class.
 */
class Context
{
  using self_type = Context;
  friend class Config;

public:
  /// Construct based a specific configuration.
  explicit Context(std::shared_ptr<Config> const &cfg);

  ~Context();

  /** Schedule a directive for a @a hook.
   *
   * @param hook Hook on which to invoke.
   * @param drtv Directive to invoke.
   * @return Errors, if any.
   */
  swoc::Errata on_hook_do(Hook hook, Directive *drtv);

  /** Invoke directives for @a hook.
   *
   * @param hook Hook index.
   * @return Errors, if any/
   */
  swoc::Errata invoke_for_hook(Hook hook);

  /** Invoke directives for remap.
   *
   * @param rule_cfg  Remap rule configuration.
   * @param rri Remap rule info from TS.
   * @return Errors, if any
   */
  swoc::Errata invoke_for_remap(Config &rule_cfg, TSRemapRequestInfo *rri);

  /** Set up to handle the hooks in the @a txn.
   *
   * @param txn TS transaction object.
   * @return @a this
   */
  self_type &enable_hooks(TSHttpTxn txn);

  /** Extract a feature.
   *
   * @param expr The feature expression.
   * @return The feature.
   *
   * This extracts the feature as described by @a expr. This feature is transient and will
   * potentially be overwritten by the next feature extraction. If the feature should be preserved
   * longer than that, use @c commit.
   *
   * The purpose of this is to permit examining the feature after extraction and before committing
   * it to transaction local memory.
   *
   * @see commit
   */
  Feature extract(Expr const &expr);

  enum ViewOption {
    EX_COMMIT, ///< Force transient to be committed
    EX_C_STR   ///< Force C-string (null terminated)
  };

  FeatureView extract_view(Expr const &expr, std::initializer_list<ViewOption> opts = {});

  /** Commit a feature.
   *
   * @param feature Feature to commit.
   * @return @a feature (after possible modification)
   *
   * This causes the feature data in @a feature to be committed such that it will no longer be
   * overwritten by future feature extractions. This @b must be called on the most recently
   * extracted feature. This may cause @a feature to be modified.
   *
   * @see extract
   */
  Feature &commit(Feature &feature);

  /** Commit a view.
   *
   * @param view View to commit.
   * @return @a Updated view.
   *
   * If @a view is in the transient buffer, or a a direct view, it is committed to the
   * context arena.
   *
   * @note @a view is not modified - the caller @b must use the return value.
   *
   * @see extract
   */
  FeatureView commit(FeatureView const &feature);

  /** Allocate and initialize a block of memory as an instance of @a T

      The template type specifies the type to create and any arguments are forwarded to the
      constructor. If the constructed object needs destruction, see @c mark_for_cleanup.
  */
  template <typename T, typename... Args> T *make(Args &&...args);

  /** Allocate context (txn scoped) space for an array of @a T.
   *
   * @tparam T Element type.
   * @param count # of elements.
   * @return A span covering the allocated array.
   *
   * This allocates in the context storage. No destructors are called when the config is destructed.
   *
   * @see mark_for_cleanup
   */
  template <typename T> swoc::MemSpan<T> alloc_span(unsigned count, size_t align = 1);

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

  /** Require @a n bytes of transient buffer.
   *
   * @param n Minimum buffer size.
   * @return @a this
   *
   * Any previous transient buffer is committed. Immediately after this method there will be at
   * least @a n bytes in the transient buffer. This is useful for string rendering when a reasonable
   * upper bound on the string length can be computed.
   */
  self_type &transient_require(size_t n);

  /** Get the transient buffer.
   *
   * @param required Amount of buffer required - optional.
   * @return The transient buffer.
   *
   * Any previous transient buffer is committed and the entire arena remnant is returned.
   * @a required is beneficial if a reasonable upper limit can be determined.
   */
  swoc::MemSpan<char> transient_buffer(size_t required = 0);

#if 0
  /** Get a transient span of a specific type.
   *
   * @tparam T Element type.
   * @param count Number of elements.
   * @return A span of @a count elemenents of type @a T
   *
   * Any previous transient span is committed. The arena remnant is resized as needed. The elements
   * of the span are not initialized, this provides only storage.
   */
  template < typename T > swoc::MemSpan<T> transient_span(unsigned count);

#endif

  /** Finalize a transient value.
   *
   * @param n Size of the value.
   * @return @a this
   *
   * This is used to indicate the transient buffer is no longer active but contains a value. If
   * the transient buffer is used the object will be committed.
   */
  self_type &transient_finalize(size_t n);

  /** Discard the current transient buffer.
   *
   * @return @a this
   *
   * The current transient buffer is discarded and the memory can be re-used. This should be called
   * in cases where the transient buffer is used only locally, otherwise it will be committed the
   * next time a transient buffer is requested.
   */
  self_type &transient_discard();

  /** Commit the current transient buffer.
   *
   * @return @a this
   *
   * If there is a transient buffer, it is committed.
   */
  self_type &commit_transient();

  /** Render text in to the transient buffer.
   *
   * @tparam F Printing functor type.
   * @param f Printing functor
   * @return A transient view of the rendered text.
   *
   * @a F must be a functor that takes a single @c BufferWriter& parameter. It should print to
   * that instance. The internal logic will call @a f and if there is an overflow will increase the
   * transient buffer and call @a f again. The output can be localized if @c commit is called
   * on the returned feature before any other transient operation.
   */
  template <typename F> FeatureView render_transient(F const &f);

#if __has_include(<memory_resource>) && _GLIBCXX_USE_CXX11_ABI
  /// Access the internal memory arena as a memory resource.
  std::pmr::memory_resource *
  pmr()
  {
    return _arena.get();
  }
#endif

  /** Convert a reserved span into memory in @a this.
   *
   * @param span Reserve span.
   * @return The context local memory.
   */
  swoc::MemSpan<void> storage_for(ReservedSpan const &span);

  /** Convert a reserved span into memory in @a this and initialize it.
   *
   * @tparam T The span type.
   * @param span The reserved span.
   * @return Initialized context local memory.
   *
   * The elements in the span are constructed in place using the constructor for @a T. The
   * initialization status is tracked per reserved span per context and only initialized once.
   * Subsequent calls for the same reserved span in the same context will not initialize the memory.
   */
  template <typename T> swoc::MemSpan<T> initialized_storage_for(ReservedSpan const &span);

  Hook _cur_hook   = Hook::INVALID;
  TSCont _cont     = nullptr;
  ts::HttpTxn _txn = nullptr;

  /// Current extracted feature.
  Feature _active;
  /// Extension for active feature when needed.
  Feature _active_ext;
  /// Feature remnant, after matching.
  FeatureView _remainder;
  /// Should the active feature be updated (e.g., is used later).
  bool _update_remainder_p = false;

  /// Context for working with PCRE - allocates from the transaction arena.
  pcre2_general_context *_rxp_ctx = nullptr;

  /** Set capture groups for a literal match.
   *
   * @param text The literal text.
   *
   * THis is used to set capture group 0 for literal matches.
   */
  void set_literal_capture(swoc::TextView text);

  /// Return the text for the active capture group at index @a idx.
  swoc::TextView active_group(int idx);

  /** BWF interface for name binding.
   *
   * @param w Output writer.
   * @param spec Specifier with name to bind.
   *
   * Generate output to @a w based on data in @a spec.
   */
  void operator()(swoc::BufferWriter &w, Extractor::Spec const &spec);

  /** Class for handling numbered arguments to formatting.
   *
   * The primary use is for mapping regular expression capture groups to indices.
   */
  class ArgPack : public swoc::bwf::ArgPack
  {
  public:
    explicit ArgPack(Context &ctx) : _ctx(ctx) {} ///< Default constructor.

    /** Get argument at index @a idx.
     *
     * @param idx Argument index.
     * @return The argument value.
     *
     * Should have BWF supply a default implementation that throws - this is used in so few
     * cases it shouldn't have to be implemented by default.
     */
    std::any capture(unsigned idx) const override;

    /** Generate formatted output for an argument.
     *
     * @param idx Argument index.
     * @param w Output.
     * @param spec Formatting specifier (should be a @c Extractor::Spec)
     * @return @a w
     *
     */
    swoc::BufferWriter &print(swoc::BufferWriter &w, swoc::bwf::Spec const &spec, unsigned idx) const override;

    /// Number of arguments in the pack.
    unsigned count() const override;

    /// Transaction context.
    Context &_ctx;
  };

  /// Wrapper for top level directives.
  /// This is used to handle both configuration level directives and directives scheduled by @c when.
  struct Callback {
    using self_type = Callback; ///< Self reference type.
  protected:
    Directive *_drtv = nullptr; ///< Directive to invoke for the callback.
    self_type *_next = nullptr; ///< Intrusive list link.
    self_type *_prev = nullptr; ///< Intrusive list link.
  public:
    /// Intrusive list descriptor.
    using Linkage = swoc::IntrusiveLinkage<self_type, &self_type::_next, &self_type::_prev>;
    /// Constructor.
    /// @param drtv Directive for the callback.
    Callback(Directive *drtv) : _drtv(drtv) {}
    /// Call the directive in @a this.
    /// @param ctx Transaction context for the callback.
    /// @return Errors, if any.
    swoc::Errata invoke(Context &ctx);
  };

  /// Directives for a particular hook.
  struct HookInfo {
    /// @c IntrusiveDList support.
    using List = swoc::IntrusiveDList<Callback::Linkage>;
    List cb_list;            ///< List of directives to call back.
    bool hook_set_p = false; ///< If a TS level callback for this hook has already been set.
  };

  /// State of each global config hook for this transaction / context.
  std::array<HookInfo, std::tuple_size<Hook>::value> _hooks;

  ts::HttpRequest ua_req_hdr();        ///< @return user agent (client) request.
  ts::HttpRequest proxy_req_hdr();     ///< @return proxy request.
  ts::HttpResponse upstream_rsp_hdr(); ///< @return upstream request.
  ts::HttpResponse proxy_rsp_hdr();    ///< @return proxy response.

  ts::HttpSsn inbound_ssn();
  ; ///< Inbound session.

  /** Store a transaction variable.
   *
   * @param name Variable name.
   * @param value Variable value.
   * @return @a this
   */
  self_type &store_txn_var(swoc::TextView const &name, Feature &&value);

  /** Store a transaction variable.
   *
   * @param name Variable name.
   * @param value Variable value.
   * @return @a this
   */
  self_type &store_txn_var(swoc::TextView const &name, Feature &value);

  /** Load a transaction variable.
   *
   * @param name Variable name.
   * @return Value of the variable.
   */
  Feature const &load_txn_var(swoc::TextView const &name);

  /// Status event returned to core after a callback has finished.
  TSEvent _global_status = TS_EVENT_HTTP_CONTINUE;

  /// Storage for remap txn information, if a remap rule is active.
  TSRemapRequestInfo *_remap_info = nullptr;
  /// Value to return from a remap invocation.
  TSRemapStatus _remap_status = TSREMAP_NO_REMAP;

  /** Working match data for doing PCRE matching.
   *
   * @param n Number of capture groups required.
   * @return Cpature data sufficient to match @a n groups.
   */
  self_type &rxp_match_require(unsigned n);

  pcre2_match_data *
  rxp_working_match_data()
  {
    return _rxp_working;
  }

  /// Commit the working match data as the active match data.
  pcre2_match_data *rxp_commit_match(swoc::TextView const &src);

  /** Make a transaction local copy of @a text that is a C string if needed.
   *
   * @param text Current string.
   * @return @a test if it is a C-string, a null terminated copy if not.
   *
   * The copy is stored in transaction scoped memory.
   */
  swoc::TextView localize_as_c_str(swoc::TextView text);

  /// Clear transaction headers - not reliable across hooks.
  void clear_cache();

  /** Mark @a ptr for cleanup when @a this is destroyed.
   *
   * @tparam T Type of @a ptr
   * @param ptr Object to clean up.
   * @return @a this
   *
   * @a ptr is cleaned up by calling @c delete.
   */
  template <typename T> self_type &mark_for_cleanup(T *ptr);

  /** Mark @a ptr for cleanup by @a clearner when @a this is destroyed.
   *
   * @tparam T Type of @a ptr
   * @param ptr Object to clean up.
   * @param cleaner Functor to clean up @a ptr.
   * @return @a this
   *
   * @a ptr is cleaned up by calling @a cleaner.
   */
  template <typename T> self_type &mark_for_cleanup(T *ptr, void (*cleaner)(T *));

  /** Get a reference to the configuration for @a this.
   *
   * @return The configuration.
   *
   * Use when local access to the configuration is needed.
   */
  Config &cfg();

  /** Get a reference to the configuration.
   *
   * @return A shared reference to the configuration to @a this.
   *
   * Use when the configuration needs to persist.
   */
  std::shared_ptr<Config> acquire_cfg();

  /** Check if the directive is marked as terminal.
   *
   * @return @c true
   */
  bool
  is_terminal() const
  {
    return _terminal_p;
  }

  /** Mark the current directive terminal status.
   *
   * @param flag Terminal status.
   * @return @a this
   *
   * If a directive is marked terminal, then it marks a point in the invocation tree for which there
   * is no backtracking. After the directive finishes invocation (which may involve invoking other
   * directives) then directive process will terminate. This must be called from the @c invoke
   * method of the directive.
   */
  self_type &
  mark_terminal(bool flag)
  {
    _terminal_p = flag;
    return *this;
  }

protected:
  /// Header for reserved memory.
  /// Default zero initialized.
  struct ReservedStatus {
    unsigned _initialized_p : 1;
  };

  /// Wrapper for top level directives.
  /// This is used to handle both configuration level directives and directives scheduled by @c when.
  struct OverflowSpan {
    using self_type = OverflowSpan; ///< Self reference type.
  protected:
    self_type *_next = nullptr; ///< Intrusive list link.
    self_type *_prev = nullptr; ///< Intrusive list link.
  public:
    /// Intrusive list descriptor.
    using Linkage = swoc::IntrusiveLinkage<self_type, &self_type::_next, &self_type::_prev>;
    /// Constructor.
    /// @param drtv Directive for the callback.
    OverflowSpan() = default;
    /// Offset of reserved span.
    /// This is also used as the key to find the correct instance.
    size_t _offset;
    /// Live memory.
    swoc::MemSpan<void> _storage;
  };

  /// Cleanup functor for an inverted arena.
  /// @internal Because the arena is inverted, calling the destructor will clean up everything.
  /// For this reason @c delete must @b not be called (that will double free).
  struct ArenaDestructor {
    void operator()(swoc::MemArena *arena);
  };

  /// Transaction local storage.
  /// This is a pointer so that the arena can be inverted to minimize allocations.
  std::unique_ptr<swoc::MemArena, ArenaDestructor> _arena;

  size_t _transient                                      = 0; ///< Current amount of reserved / temporary space in the arena.
  static constexpr decltype(_transient) TRANSIENT_ACTIVE = std::numeric_limits<decltype(_transient)>::max();

  // HTTP header objects for the transaction.
  ts::HttpRequest _ua_req;        ///< Client request header.
  ts::HttpRequest _proxy_req;     ///< Proxy request header.
  ts::HttpResponse _upstream_rsp; ///< Upstream response header.
  ts::HttpResponse _proxy_rsp;    ///< Proxy response header.

  /// Base / Global configuration object.
  std::shared_ptr<Config> _cfg;

  /// Directive shared storage.
  swoc::MemSpan<void> _ctx_store;

  /// Active regex capture data.
  pcre2_match_data *_rxp_active = nullptr;

  /// Temporary / working capture group data.
  /// If successful, this becomes active via @c rxp_commit_match
  /// @see rxp_commit_match
  pcre2_match_data *_rxp_working = nullptr;

  /// Number of capture groups supported by current match data allocations.
  unsigned _rxp_n = 0;

  /// Active full to which the capture groups refer.
  FeatureView _rxp_src;

  /// Additional clean up needed when @a this is destroyed.
  swoc::IntrusiveDList<Finalizer::Linkage> _finalizers;

  /// List of overflaw reserved spans.
  swoc::IntrusiveDList<OverflowSpan::Linkage> _overflow_spans;

  /// A transaction scope variable.
  struct TxnVar {
    using self_type = TxnVar; ///< Self reference type.
    static constexpr std::hash<std::string_view> Hash_Func{};

    swoc::TextView _name;       ///< Name of variable.
    Feature _value;             ///< Value of variable.
    self_type *_next = nullptr; ///< Intrusive link.
    self_type *_prev = nullptr; ///< Intrusive link.

    /// Linkage for @c IntrusiveHashMap.
    struct Linkage : public swoc::IntrusiveLinkage<self_type, &self_type::_next, &self_type::_prev> {
      static swoc::TextView
      key_of(self_type *self)
      {
        return self->_name;
      }
      static auto
      hash_of(swoc::TextView const &text) -> decltype(Hash_Func(text))
      {
        return Hash_Func(text);
      }
      static bool
      equal(swoc::TextView const &lhs, swoc::TextView const &rhs)
      {
        return lhs == rhs;
      }
    };

    TxnVar(swoc::TextView const &name, Feature const &value) : _name(name), _value(value) {}
  };

  using TxnVariables = swoc::IntrusiveHashMap<TxnVar::Linkage>;
  TxnVariables _txn_vars; ///< Variables for the transaction.

  /// Internal named object local to the context.
  struct NamedObject {
    using self_type = NamedObject; ///< Self reference type.
    static constexpr std::hash<std::string_view> Hash_Func{};

    swoc::TextView _name;       ///< Name of variable.
    swoc::MemSpan<void> _span;  ///< Object memroy.
    self_type *_next = nullptr; ///< Intrusive link.
    self_type *_prev = nullptr; ///< Intrusive link.

    /// Linkage for @c IntrusiveHashMap.
    struct Linkage : public swoc::IntrusiveLinkage<self_type, &self_type::_next, &self_type::_prev> {
      static swoc::TextView
      key_of(self_type *self)
      {
        return self->_name;
      }
      static auto
      hash_of(swoc::TextView const &text) -> decltype(Hash_Func(text))
      {
        return Hash_Func(text);
      }
      static bool
      equal(swoc::TextView const &lhs, swoc::TextView const &rhs)
      {
        return lhs == rhs;
      }
    };

    NamedObject(swoc::TextView const &name, swoc::MemSpan<void> span) : _name(name), _span(span) {}
  };

  using NamedObjects = swoc::IntrusiveHashMap<NamedObject::Linkage>;
  NamedObjects _named_objects; ///< Conext local data objects.

  /// Flag for continuing invoking directives.
  bool _terminal_p = false;

  /// Invoke the callbacks for the current hook.
  swoc::Errata invoke_callbacks();

  swoc::MemSpan<void> overflow_storage_for(ReservedSpan const &span);

  /// Used for generating transient feature expression values.
  std::optional<swoc::FixedBufferWriter> _transient_writer;

  /** Entry point from TS via plugin API.
   *
   * @param cont TS Continuation.
   * @param evt Event type.
   * @param payload Ignored.
   * @return 0
   *
   * The @c Context instance is carried as the Continuation data.
   */
  static int ts_callback(TSCont cont, TSEvent evt, void *payload);
};

// --- Implementation ---

inline auto
Context::store_txn_var(swoc::TextView const &name, Feature &&value) -> self_type &
{
  return this->store_txn_var(name, value);
}

template <typename T>
Context &
Context::mark_for_cleanup(T *ptr)
{
  auto f = _arena->make<Finalizer>(ptr, [](void *ptr) { std::destroy_at(static_cast<T *>(ptr)); });
  _finalizers.append(f);
  return *this;
}

template <typename T>
Context &
Context::mark_for_cleanup(T *ptr, void (*cleaner)(T *))
{
  _finalizers.append(_arena->make<Finalizer>(ptr, cleaner));
  return *this;
}

template <typename T>
swoc::MemSpan<T>
Context::alloc_span(unsigned count, size_t align)
{
  this->commit_transient();
  return _arena->alloc(sizeof(T) * count, align).rebind<T>();
}

inline swoc::MemSpan<void>
Context::storage_for(ReservedSpan const &span)
{
  if (span.offset + span.n <= _ctx_store.size()) {
    return _ctx_store.subspan(span.offset, span.n);
  }
  return this->overflow_storage_for(span);
}

template <typename T>
swoc::MemSpan<T>
Context::initialized_storage_for(ReservedSpan const &span)
{
  auto mem               = this->storage_for(span).rebind<T>();
  ReservedStatus &status = *reinterpret_cast<ReservedStatus *>(reinterpret_cast<std::byte *>(mem.data()) -
                                                               swoc::Scalar<8>(swoc::round_up(sizeof(ReservedStatus))));
  if (!status._initialized_p) {
    mem.apply([](T &t) { new (&t) T; });
    status._initialized_p = true;
  }
  return mem;
}

inline Config &
Context::cfg()
{
  return *_cfg;
}

inline std::shared_ptr<Config>
Context::acquire_cfg()
{
  return _cfg;
}

inline void
Context::ArenaDestructor::operator()(swoc::MemArena *arena)
{
  arena->swoc::MemArena::~MemArena();
}

inline void
Context::clear_cache()
{
  _ua_req.clear();
  _proxy_req.clear();
  _upstream_rsp.clear();
  _proxy_rsp.clear();
}

inline auto
Context::transient_finalize(size_t n) -> self_type &
{
  _transient = n;
  return *this;
}

inline Context::self_type &
Context::transient_discard()
{
  _transient = 0;
  return *this;
}

template <typename F>
FeatureView
Context::render_transient(F const &f)
{
  size_t base  = 0;     // rendered size.
  bool outer_p = false; // outermost / top level render.
  // Tricksy - if there's no extant writer, then this is the outer most render and needs to both
  // create the writer and clean it up. Also, the outer is responsible for finalizing the
  // transient buffer used.
  if (!_transient_writer.has_value()) {
    _transient_writer.template emplace(this->transient_buffer());
    outer_p = true;
  } else {
    base = _transient_writer->extent();
  }
  // Do the two phase writing - on error, outer is responsible for retrying.
  f(*_transient_writer);
  auto n = _transient_writer->extent();
  if (_transient_writer->error() && outer_p) {
    _transient_writer->assign(this->transient_discard().transient_require(n).transient_buffer());
    f(*_transient_writer);
    n = _transient_writer->extent();
  }
  auto v = _transient_writer->view();
  v.remove_prefix(base);
  if (outer_p) {
    this->transient_finalize(n);
    _transient_writer.reset();
  }
  return v;
}

template <typename T, typename... Args>
T *
Context::make(Args &&...args)
{
  this->commit_transient();
  return new (this->_arena->alloc(sizeof(T)).data()) T(std::forward<Args>(args)...);
}

inline ts::HttpSsn
Context::inbound_ssn()
{
  return _txn.inbound_ssn();
}

template <typename T, typename... Args>
T *
Context::obtain_named_object(swoc::TextView name, Args &&...args)
{
  auto spot = _named_objects.find(name);
  if (spot != _named_objects.end()) {
    return static_cast<T *>(spot->_span.data());
  }

  auto span = this->alloc_span<T>(1, alignof(T));
  _named_objects.insert(this->make<NamedObject>(name, span));
  return new (span.data()) T(std::forward<Args>(args)...);
}

template <typename T>
T *
Context::named_object(swoc::TextView name)
{
  auto spot = _named_objects.find(name);
  return spot != _named_objects.end() ? static_cast<T *>(spot->_span.data()) : nullptr;
}
