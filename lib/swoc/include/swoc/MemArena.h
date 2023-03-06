// SPDX-License-Identifier: Apache-2.0
// Copyright Verizon Media 2020
/** @file

    Memory arena for allocations
 */

#pragma once

#include <mutex>
#include <memory>
#include <utility>
#include <new>
#if __has_include(<memory_resource>)
#include <memory_resource>
#endif

#include "swoc/MemSpan.h"
#include "swoc/Scalar.h"
#include "swoc/IntrusiveDList.h"

namespace swoc { inline namespace SWOC_VERSION_NS {
/** A memory arena.

    The intended use is for allocating many small chunks of memory - few, large allocations are best
    handled through other mechanisms. The purpose is to amortize the cost of allocation of each
    chunk across larger internal allocations ("reserving memory"). In addition the allocated memory
    chunks are presumed to have similar lifetimes so all of the memory in the arena can be released
    when the arena is destroyed.
 */
class MemArena
#if __has_include(<memory_resource>)
  : public std::pmr::memory_resource
#endif
{
  using self_type = MemArena; ///< Self reference type.

public:
  static constexpr size_t DEFAULT_ALIGNMENT{1}; ///< Default memory alignment.

  /// Functor for destructing a self contained arena.
  /// @see MemArena::unique_ptr
  static inline auto destroyer = std::destroy_at<MemArena>;

  /// Correct type for a unique pointer to an instance.
  /// Initialization is
  /// @code
  ///     MemArena::unique_ptr arena(nullptr, MemArena::destroyer);
  /// @endcode
  /// To create the arena on demand
  /// @code
  ///    arena.reset(MemArena::construct_self_contained());
  /// @endcode
  /// If the unique pointer is to be initialized with an arena, it should probably be a direct member isntead.
  using unique_ptr = std::unique_ptr<self_type, void (*)(self_type *)>;

  /// Simple internal arena block of memory. Maintains the underlying memory.
  struct Block {
    /// A block must have at least this much free space to not be "full".
    static constexpr size_t MIN_FREE_SPACE = 16;

    /// Get the start of the data in this block.
    char *data();

    /// Get the start of the data in this block.
    const char *data() const;

    /// Amount of unallocated storage.
    size_t remaining() const;

    /** Compute the padding needed such adding it to @a ptr is a multiple of @a align.
     *
     * @param ptr Base pointer.
     * @param align Alignment requirement (must be a power of 2).
     * @return Value to add to @a ptr to achieve @a align.
     */
    static size_t align_padding(void const *ptr, size_t align);

    /** Check if there is @a n bytes of space at @a align.
     *
     * @param n Size required.
     * @param align Alignment required.
     * @return @c true if there is space, @c false if not.
     */
    bool satisfies(size_t n, size_t align) const;

    /// Span of unallocated storage.
    MemSpan<void> remnant();

    /** Allocate @a n bytes from this block.
     *
     * @param n Number of bytes to allocate.
     * @param align Alignment requirement (default, no alignment).
     * @return The span of memory allocated.
     */
    MemSpan<void> alloc(size_t n, size_t = DEFAULT_ALIGNMENT);

    /** Discard allocations.
     *
     * Reset the block state to empty.
     *
     * @return @a this.
     */
    Block &discard();

    /** Check if the byte at address @a ptr is in this block.
     *
     * @param ptr Address of byte to check.
     * @return @c true if @a ptr is in this block, @c false otherwise.
     */
    bool contains(const void *ptr) const;

    /// @return @c true if the block has at least @c MIN_FREE_SPACE bytes free.
    bool is_full() const;

  protected:
    friend MemArena; ///< Container.

    /** Override @c operator @c delete.
     *
     * This is required because the allocated memory size is larger than the class size which
     * requires calling @c free directly, skipping the destructor and avoiding complaints about size
     * mismatches.
     *
     * @param ptr Memory to be de-allocated.
     */
    static void operator delete(void *ptr) noexcept;

    /** Override placement (non-allocated) @c delete.
     *
     * @param ptr Pointer returned from @c new
     * @param place Value passed to @c new.
     *
     * This is called only when the class constructor throws an exception during placement new.
     *
     * @note I think the parameters are described correctly, the documentation I can find is a bit
     * vague on the source of these values. It is required even if the constructor is marked @c
     * noexcept. Both are kept in order to be documented.
     *
     * @internal This is required by ICC, but not GCC. Annoying, but it appears this is a valid
     * interpretation of the spec. In practice this is never called because the constructor does
     * not throw.
     */
    static void operator delete([[maybe_unused]] void *ptr, void *place) noexcept;

    /** Construct to have @a n bytes of available storage.
     *
     * Note this is descriptive - this presumes use via placement new and the size value describes
     * memory already allocated immediately after this instance.
     * @param n The amount of storage.
     */
    explicit Block(size_t n) noexcept;

    size_t size;         ///< Actual block size.
    size_t allocated{0}; ///< Current allocated (in use) bytes.

    struct Linkage {
      /// @cond INTERNAL_DETAIL
      Block *_next{nullptr};
      Block *_prev{nullptr};

      static Block *&next_ptr(Block *);

      static Block *&prev_ptr(Block *);
      /// @endcond
    } _link; ///< Intrusive list support.
  };

  /// Intrusive list of blocks.
  using BlockList = IntrusiveDList<Block::Linkage>;

  /** Construct with reservation hint.
   *
   * No memory is initially reserved, but when memory is needed this will be done so at least
   * @a n bytes of available memory is reserved.
   *
   * To pre-reserve call @c alloc(0), e.g.
   * @code
   * MemArena arena(512); // Make sure at least 512 bytes available in first block.
   * arena.alloc(0); // Force allocation of first block.
   * @endcode
   *
   * @param n Minimum number of available bytes in the first internally reserved block.
   */
  explicit MemArena(size_t n = DEFAULT_BLOCK_SIZE);

  /** Construct using static block.
   *
   * @param static_block A block of memory that is non-deletable.
   *
   * @a static_block is used as the first block for allocation and is never deleted. This makes
   * it possible to have an instance that allocates from stack memory and only allocates from the
   * heap if the static block becomes full.
   *
   * @note There is no default block size because the static block is the initial block. Subsequent
   * allocations are based on that size.
   */
  explicit MemArena(MemSpan<void> static_block);

  /// no copying
  MemArena(self_type const &that) = delete;

  /// Allow moving the arena.
  MemArena(self_type &&that);

  /// Destructor.
  ~MemArena();

  /// No copy assignment.
  self_type & operator=(self_type const & that) = delete;

  /// Move assignment.
  self_type & operator=(self_type &&that);

  /** Make a self-contained instance.
   *
   * @param n The initial memory size hint.
   * @return A new, self contained instance.
   *
   * Create an instance of @c MemArena that is stored in its own memory pool. The size hint @a n
   * is adjusted to account for the space consumed by the @c MemArena instance. This instance
   * will therefore always have done its initial internal memory allocation to provide space
   * for itself.
   *
   * This is most useful for smaller objects that need to strongly minimize their size when not
   * allocating memory. In that context, this enables being able to have a memory pool as needed
   * at the cost of a only single pointer in the instance.
   *
   * @note This requires careful attention to detail for freezing and thawing, as the @c MemArena
   * itself will be in the frozen memory and must be moved to the fresh allocation.
   *
   * @note @c delete must not be called on the returned pointer. Instead the @c MemArena destructor
   * must be explicitly called, which will clean up all of the allocated memory. See the
   * documentation for further details.
   */
  static self_type *construct_self_contained(size_t n = DEFAULT_BLOCK_SIZE);

  /** Allocate @a n bytes of storage.

      Returns a span of memory within the arena. alloc() is self expanding but DOES NOT self
      coalesce. This means that no matter the arena size, the caller will always be able to alloc()
      @a n bytes.

      @param n number of bytes to allocate.
      @param align Required alignment, defaults to 1 (no alignment). Must be a power of 2.
      @return a MemSpan of the allocated memory.
   */
  MemSpan<void> alloc(size_t n, size_t align = DEFAULT_ALIGNMENT);

  /** ALlocate a span of memory sufficient for @a n instance of @a T.
   *
   * @tparam T Element type.
   * @param n Number of instances.
   * @return A span large enough to hold @a n instances of @a T.
   *
   * The instances are @b not initialized / constructed. This only allocates the memory.
   * This is handy for types that don't need initialization, such as built in types like @c int.
   * @code
   *   auto vec = arena.alloc_span<int>(20); // allocate space for 20 ints
   * @endcode
   *
   * The memory is aligned according to @c alignof(T).
   */
  template <typename T> MemSpan<T> alloc_span(size_t n);

  /** Allocate and initialize a block of memory as an instance of @a T

      The template type specifies the type to create and any arguments are forwarded to the
      constructor. Example:

      @code
      struct Thing { ... };
      auto thing = arena.make<Thing>(...constructor args...);
      @endcode

      Do @b not call @c delete an object created this way - that will attempt to free the memory and
      break. A destructor may be invoked explicitly but the point of this class is that no object in
      it needs to be deleted, the memory will all be reclaimed when the Arena is destroyed. In
      general it is a bad idea to make objects in the Arena that own memory that is not also in the
      Arena.
  */
  template <typename T, typename... Args> T *make(Args &&... args);

  /** Freeze reserved memory.

      All internal memory blocks are frozen and will not be involved in future allocations.
      Subsequent allocation will reserve new internal blocks. By default the first reserved block
      will be large enough to contain all frozen memory. If this is not correct a different target
      can be specified as @a n.

      @param n Target number of available bytes in the next reserved internal block.
      @return @c *this
   */
  MemArena &freeze(size_t n = 0);

  /** Unfreeze arena.
   *
   * Frozen memory is released.
   *
   * @return @c *this
   */
  self_type &thaw();

  /** Release all memory.

      Empties the entire arena and deallocates all underlying memory. The hint for the next reserved
      block size will be @a n if @a n is not zero, otherwise it will be the sum of all allocations
      when this method was called.

      @param hint Size hint for the next internal allocation.
      @return @a this

      @see discard

   */
  MemArena &clear(size_t hint = 0);

  /** Discard all allocations.
   *
   * All active internal memory blocks are reset to be empty, discarding any allocations. These blocks
   * will be re-used by subsequent allocations.
   *
   * @param hint Size hint for the next internal allocation.
   * @return @a this.
   *
   * @see clear
   */
  MemArena &discard(size_t hint = 0);

  /// @return The amount of memory allocated.
  size_t size() const;

  /// @return The amount of free space.
  size_t remaining() const;

  /** Get aligned and sized remnant.
   *
   * @tparam T Element type.
   * @param n Number of instances of @a T
   * @return A span that is in the remnant, correctly aligned with minimal padding.
   *
   * This is guaranteed to be the same bytes as if @c alloc<T> was called. The returned span will
   * always be the specified size, the remnant will be expanded as needed.
   */
  template <typename T> MemSpan<T> remnant_span(size_t n);

  /// @return Contiguous free space in the current internal block.
  MemSpan<void> remnant();

  /** Get an aligned remnant.
   *
   * @param n Remnant size.
   * @param align Memory alignment (default 1, must be power of 2).
   * @return Space in the remnant with minimal alignment padding.
   *
   * @note This will always return a span of @a n bytes, the remnant will be expanded as needed.
   */
  MemSpan<void> remnant(size_t n, size_t align = DEFAULT_ALIGNMENT);

  /** Require @a n bytes of contiguous memory to be available for allocation.
   *
   * @param n Number of bytes.
   * @param align Align requirement (default is 1, no alignment).
   * @return @a this
   *
   * This forces the @c remnant to be at least @a n bytes of contiguous memory. A subsequent
   * @c alloc will use this space if the allocation size is at most the remnant size.
   */
  self_type &require(size_t n, size_t align = DEFAULT_ALIGNMENT);

  /// @returns the total number of bytes allocated within the arena.
  size_t allocated_size() const;

  /** Check if a the byte at @a ptr is in memory owned by this arena.
   *
   * @param ptr Address of byte to check.
   * @return @c true if the byte at @a ptr is in the arena, @c false if not.
   */
  bool contains(const void *ptr) const;

  /** Total memory footprint, including wasted space.
   * @return Total memory footprint.
   */
  size_t reserved_size() const;

  using const_iterator = BlockList::const_iterator; ///< Constant element iteration.
  using iterator       = const_iterator; ///< Element iteration.

  /// First active block.
  const_iterator begin() const;

  /// After Last active block.
  const_iterator end() const;

  /// First frozen block.
  const_iterator frozen_begin() const;

  /// After last frozen block.
  const_iterator frozen_end() const;
protected:
  /** Internally allocates a new block of memory of size @a n bytes.
   *
   * @param n Size of block to allocate.
   * @return
   */
  Block *make_block(size_t n);

  /// Clean up the frozen list.
  void destroy_frozen();

  /// Clean up the active list
  void destroy_active();

  using Page        = Scalar<4096>;            ///< Size for rounding block sizes.
  using QuarterPage = Scalar<Page::SCALE / 4>; ///< Quarter page - unit for sub page sizes.
  using Paragraph   = Scalar<16>;              ///< Minimum unit of memory allocation.

  static constexpr size_t ALLOC_HEADER_SIZE = 16; ///< Guess of overhead of @c malloc
  /// Initial block size to allocate if not specified via API.
  static constexpr size_t DEFAULT_BLOCK_SIZE = Page::SCALE - Paragraph{round_up(ALLOC_HEADER_SIZE + sizeof(Block))};

  size_t _active_allocated = 0; ///< Total allocations in the active generation.
  size_t _active_reserved  = 0; ///< Total current reserved memory.
  /// Total allocations in the previous generation. This is only non-zero while the arena is frozen.
  size_t _frozen_allocated = 0;
  /// Total frozen reserved memory.
  size_t _frozen_reserved = 0;

  /// Minimum free space needed in the next allocated block.
  /// This is not zero iff @c reserve was called.
  size_t _reserve_hint = 0;

  BlockList _frozen; ///< Previous generation, frozen memory.
  BlockList _active; ///< Current generation. Allocate here.

  /// Static block, if any.
  Block *_static_block = nullptr;

  // Note on _active block list - blocks that become full are moved to the end of the list.
  // This means that when searching for a block with space, the first full block encountered
  // marks the last block to check. This keeps the set of blocks to check short.

private:
#if __has_include(<memory_resource>)
  // PMR support methods.

  /// PMR allocation.
  void *do_allocate(std::size_t bytes, std::size_t align) override;

  /// PMR de-allocation.
  /// Does nothing.
  void do_deallocate(void *, size_t, size_t) override;

  /// PMR comparison of memory resources.
  /// @return @c true only if @a that is the same instance as @a this.
  bool do_is_equal(std::pmr::memory_resource const &that) const noexcept override;
#endif
};

/** Arena of a specific type on top of a @c MemArena.
 *
 * @tparam T Type in the arena.
 *
 * A pool of unused / free instances of @a T is kept for reuse. If none are available then a new
 * instance is allocated from the arena.
 */
template <typename T> class FixedArena {
  using self_type = FixedArena; ///< Self reference type.
protected:
  /// Rebinding type for instances on the free list.
  struct Item {
    Item *_next; ///< Next item in the free list.
  };

  Item _list{nullptr}; ///< List of dead instances.
  MemArena &_arena;    ///< Memory source.

public:
  /** Construct a pool.
   *
   * @param arena The arena for memory.
   */
  explicit FixedArena(MemArena &arena);

  /** Create a new instance.
   *
   * @tparam Args Constructor argument types.
   * @param args Constructor arguments.
   * @return A new instance of @a T.
   */
  template <typename... Args> T *make(Args... args);

  /** Destroy an instance.
   *
   * @param t The instance to destroy.
   *
   * The instance is destructed and then put on the free list for re-use.
   */
  void destroy(T *t);

  /// Drop all items in the free list.
  void clear();

  /// Access the wrapped arena directly.
  MemArena & arena();
};

// --- Implementation ---
/// @cond INTERNAL_DETAIL

inline auto
MemArena::Block::Linkage::next_ptr(Block *b) -> Block *& {
  return b->_link._next;
}

inline auto
MemArena::Block::Linkage::prev_ptr(Block *b) -> Block *& {
  return b->_link._prev;
}

inline MemArena::Block::Block(size_t n) noexcept : size(n) {}

inline char *
MemArena::Block::data() {
  return reinterpret_cast<char *>(this + 1);
}

inline const char *
MemArena::Block::data() const {
  return reinterpret_cast<const char *>(this + 1);
}

inline bool
MemArena::Block::contains(const void *ptr) const {
  const char *base = this->data();
  return base <= ptr && ptr < base + size;
}

inline size_t
MemArena::Block::remaining() const {
  return size - allocated;
}

inline bool
MemArena::Block::is_full() const {
  return this->remaining() < MIN_FREE_SPACE;
}

inline MemSpan<void>
MemArena::Block::alloc(size_t n, size_t align) {
  auto base = this->data() + allocated;
  auto pad  = align_padding(base, align);
  if ((n + pad) > this->remaining()) {
    throw(std::invalid_argument{"MemArena::Block::alloc size is more than remaining."});
  }
  MemSpan<void> zret = this->remnant().prefix(n + pad);
  zret.remove_prefix(pad);
  allocated += n + pad;
  return zret;
}

inline MemSpan<void>
MemArena::Block::remnant() {
  return {this->data() + allocated, this->remaining()};
}

inline MemArena::Block &
MemArena::Block::discard() {
  allocated = 0;
  return *this;
}

inline void
MemArena::Block::operator delete(void *ptr) noexcept {
  ::free(ptr);
}
inline void
MemArena::Block::operator delete([[maybe_unused]] void *ptr, void *place) noexcept {
  ::free(place);
}

inline size_t
MemArena::Block::align_padding(void const *ptr, size_t align) {
  if (auto delta = uintptr_t(ptr) & (align - 1) ; delta > 0) {
    return align - delta;
  }
  return 0;
}

inline MemArena::MemArena(size_t n) : _reserve_hint(n) {}

template <typename T>
MemSpan<T>
MemArena::alloc_span(size_t n) {
  return this->alloc(sizeof(T) * n, alignof(T)).rebind<T>();
}

template <typename T, typename... Args>
T *
MemArena::make(Args &&... args) {
  return new (this->alloc(sizeof(T), alignof(T)).data()) T(std::forward<Args>(args)...);
}

template <typename T>
MemSpan<T>
MemArena::remnant_span(size_t n) {
  auto span = this->require(sizeof(T) * n, alignof(T)).remnant();
  return span.remove_prefix(Block::align_padding(span.data(), alignof(T))).rebind<T>();
}

template <>
inline MemSpan<void>
MemArena::remnant_span<void>(size_t n) { return this->require(n).remnant().prefix(n); }

inline MemSpan<void>
MemArena::remnant(size_t n, size_t align) { return this->require(n, align).remnant().prefix(n); }

inline size_t
MemArena::size() const {
  return _active_allocated;
}

inline size_t
MemArena::allocated_size() const {
  return _frozen_allocated + _active_allocated;
}

inline size_t
MemArena::remaining() const {
  return _active.empty() ? 0 : _active.head()->remaining();
}

inline MemSpan<void>
MemArena::remnant() {
  return _active.empty() ? MemSpan<void>() : _active.head()->remnant();
}

inline size_t
MemArena::reserved_size() const {
  return _active_reserved + _frozen_reserved;
}

inline auto
MemArena::begin() const -> const_iterator {
  return _active.begin();
}

inline auto
MemArena::end() const -> const_iterator {
  return _active.end();
}

inline auto
MemArena::frozen_begin() const -> const_iterator {
  return _frozen.begin();
}

inline auto
MemArena::frozen_end() const -> const_iterator {
  return _frozen.end();
}

template <typename T> FixedArena<T>::FixedArena(MemArena &arena) : _arena(arena) {
  static_assert(sizeof(T) >= sizeof(T *));
}

template <typename T>
template <typename... Args>
T *
FixedArena<T>::make(Args... args) {
  if (_list._next) {
    void *t     = _list._next;
    _list._next = _list._next->_next;
    return new (t) T(std::forward<Args>(args)...);
  }
  return _arena.template make<T>(std::forward<Args>(args)...);
}

template <typename T>
void
FixedArena<T>::destroy(T *t) {
  if (t) {
    t->~T(); // destructor.
    auto item   = reinterpret_cast<Item *>(t);
    item->_next = _list._next;
    _list._next = item;
  }
}

template <typename T>
void
FixedArena<T>::clear() {
  _list._next = nullptr;
}

template < typename T > MemArena & FixedArena<T>::arena() { return _arena; }

/// @endcond INTERNAL_DETAIL

}} // namespace swoc::SWOC_VERSION_NS
