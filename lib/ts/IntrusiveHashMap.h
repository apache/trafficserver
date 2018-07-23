/** @file

  Instrusive hash map.

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
*/

#pragma once

#include <vector>
#include <array>
#include <algorithm>
#include <ts/IntrusiveDList.h>

/** Intrusive Hash Table.

    Values stored in this container are not destroyed when the container is destroyed or removed from the container.
    They must be released by the client.

    Duplicate keys are allowed. Clients must walk the list for multiple entries.
    @see @c Location::operator++()

    By default the table automatically expands to limit the average chain length. This can be tuned. If set
    to @c MANUAL then the table will expand @b only when explicitly requested to do so by the client.
    @see @c ExpansionPolicy
    @see @c setExpansionPolicy()
    @see @c setExpansionLimit()
    @see @c expand()

    The hash table is configured by a descriptor class. This must contain the following members

    - The static method <tt>key_type key_of(value_type *)</tt> which returns the key for an instance of @c value_type.

    - The static method <tt>bool equal(key_type lhs, key_type rhs)</tt> which checks if two instances of @c Key are the same.

    - The static method <tt>hash_id hash_of(key_type)</tt> which computes the hash value of the key. @c ID must a numeric type.

    - The static method <tt>value_type *& next_ptr(value_type *)</tt> which returns a reference to a forward pointer.

    - The static method <tt>value_type *& prev_ptr(value_type *)</tt> which returns a reference to a backwards pointer.

    These are the required members, it is permitted to have other methods (if the descriptor is used for other purposes)
    or to provide overloads of the methods. Note this is compatible with @c IntrusiveDList.

    Several internal types are deduced from these arguments.

    @a Key is the return type of @a key_of and represents the key that distinguishes instances of @a value_type. Two
    instances of @c value_type are considered the same if their respective @c Key values are @c equal. @c Key is
    presumed cheap to copy. If the underlying key is not a simple type then @a Key should be a constant pointer or a
    constant reference. The hash table will never attempt to modify a key.

    @a ID The numeric type that is the hash value for an instance of @a Key.

    Example for @c HttpServerSession keyed by the origin server IP address.

    @code
    struct Descriptor {
      static sockaddr const* key_of(HttpServerSession const* value) { return &value->ip.sa }
      static bool equal(sockaddr const* lhs, sockaddr const* rhs) { return ats_ip_eq(lhs, rhs); }
      static uint32_t hash_of(sockaddr const* key) { return ats_ip_hash(key); }
      static HttpServerSession *& next_ptr(HttpServerSession * ssn) { return ssn->_next; }
      static HttpServerSession *& prev_ptr(HttpServerSession * ssn) { return ssn->_prev; }
    };
    using Table = IntrusiveHashMap<Descriptor>;
    @endcode

 */
template <typename H> class IntrusiveHashMap
{
  using self_type = IntrusiveHashMap;

public:
  /// Type of elements in the map.
  using value_type = typename std::remove_pointer<typename std::remove_reference<decltype(H::next_ptr(nullptr))>::type>::type;
  /// Key type for the elements.
  using key_type = decltype(H::key_of(static_cast<value_type *>(nullptr)));
  /// The numeric hash ID computed from a key.
  using hash_id = decltype(H::hash_of(H::key_of(static_cast<value_type *>(nullptr))));

  /// When the hash table is expanded.
  enum ExpansionPolicy {
    MANUAL,  ///< Client must explicitly expand the table.
    AVERAGE, ///< Table expands if average chain length exceeds limit. [default]
    MAXIMUM  ///< Table expands if any chain length exceeds limit.
  };

protected:
  /** List of elements.
   * All table elements are in this list. The buckets reference their starting element in the list, or nothing if
   * no elements are in that bucket.
   */
  using List = IntrusiveDList<H>;

  /// A bucket for the hash map.
  struct Bucket {
    /// Support for IntrusiveDList<Bucket::Linkage>, definitions and link storage.
    struct Linkage {
      static Bucket *&next_ptr(Bucket *b); ///< Access next pointer.
      static Bucket *&prev_ptr(Bucket *b); ///< Access prev pointer.
      Bucket *_prev{nullptr};              ///< Prev pointer.
      Bucket *_next{nullptr};              ///< Next pointer.
    } _link;

    value_type *_v{nullptr}; ///< First element in the bucket.
    size_t _count{0};        ///< Number of elements in the bucket.

    /** Marker for the chain having different keys.

        This is used to determine if expanding the hash map would be useful - buckets that are not mixed
        will not be changed by an expansion.
     */
    bool _mixed_p{false};

    /// Compute the limit value for iteration in this bucket.
    /// This is the value of the next bucket, or @c nullptr if no next bucket.
    value_type *limit() const;

    /// Verify @a v is in this bucket.
    bool contains(value_type *v) const;

    void clear(); ///< Reset to initial state.
  };

public:
  /// The default starting number of buckets.
  static size_t constexpr DEFAULT_BUCKET_COUNT = 7; ///< POOMA.
  /// The default expansion policy limit.
  static size_t constexpr DEFAULT_EXPANSION_LIMIT = 4; ///< Value from previous version.
  /// Expansion policy if not specified in constructor.
  static ExpansionPolicy constexpr DEFAULT_EXPANSION_POLICY = AVERAGE;

  using iterator       = typename List::iterator;
  using const_iterator = typename List::const_iterator;

  /// A range of elements in the map.
  /// It is a half open range, [first, last) in the usual STL style.
  /// @internal I tried @c std::pair as a base for this but was unable to get STL container operations to work.
  struct range : public std::pair<iterator, iterator> {
    using super_type = std::pair<iterator, iterator>; ///< Super type.
    using super_type::super_type;                     ///< Use super type constructors.

    // These methods enable treating the range as a view in to the hash map.

    /// Return @a first
    iterator const &begin() const;
    /// Return @a last
    iterator const &end() const;
  };

  /// A range of constant elements in the map.
  struct const_range : public std::pair<const_iterator, const_iterator> {
    using super_type = std::pair<const_iterator, const_iterator>; ///< Super type.

    /// Allow implicit conversion of range to const_range.
    const_range(range const &r);

    using super_type::super_type; ///< Use super type constructors.

    // These methods enable treating the range as a view in to the hash map.

    /// Return @a first
    const_iterator const &begin() const;
    /// Return @a last
    const_iterator const &end() const;
  };

  /// Construct, starting with @n buckets.
  /// This doubles as the default constructor.
  IntrusiveHashMap(size_t n = DEFAULT_BUCKET_COUNT);

  /** Remove all values from the table.

      The values are not cleaned up. The values are not touched in this method, therefore it is safe
      to destroy them first and then @c clear this table.
  */
  self_type &clear();

  iterator begin();             ///< First element.
  const_iterator begin() const; ///< First element.
  iterator end();               ///< Past last element.
  const_iterator end() const;   ///< Past last element.

  /** Insert a value in to the table.
      The @a value must @b NOT already be in a table of this type.
      @note The value itself is put in the table, @b not a copy.
  */
  void insert(value_type *v);

  /** Find an element with a key equal to @a key.

      @return A element with a matching key, or the end iterator if not found.
  */
  const_iterator find(key_type key) const;
  iterator find(key_type key);

  /** Get an iterator for an existing value @a v.

      @return An iterator that references @a v, or the end iterator if @a v is not in the table.
  */
  const_iterator find(value_type const *v) const;
  iterator find(value_type *v);

  /** Find the range of objects with keys equal to @a key.

      @return A iterator pair of [first, last) items with equal keys.
  */
  const_range equal_range(key_type key) const;
  range equal_range(key_type key);

  /** Get an @c iterator for the value @a v.

      This is a bit obscure but needed in certain cases. Because the interface assumes iterators are always valid
      this avoid containment checks, which is useful if @a v is already known to be in the container.
   */
  iterator iterator_for(value_type *v);
  const_iterator iterator_for(const value_type *v) const;

  /** Remove the value at @a loc from the table.

      @note This does @b not clean up the removed elements. Use carefully to avoid leaks.

      @return An iterator the next value past @a loc. [STL compatibility]
  */
  iterator erase(iterator const &loc);

  /// Remove all elements in the @c range @a r.
  iterator erase(range const &r);

  /// Remove all elements in the range (start, limit]
  iterator erase(iterator const &start, iterator const &limit);

  /// Remove a @a value from the container.
  /// @a value is checked for being a member of the container.
  /// @return @c true if @a value was in the container and removed, @c false if it was not in the container.
  bool erase(value_type *value);

  /** Apply @a F(value_type&) to every element in the hash map.
   *
   * This is similar to a range for loop except the iteration is done in a way where destruction or alternation of
   * the element does @b not affect the iterator. Primarily this is useful for @c delete to clean up the elements
   * but it can have other uses.
   *
   * @tparam F A functional object of the form <tt>void F(value_type&)</tt>
   * @param f The function to apply.
   * @return
   */
  template <typename F> self_type &apply(F &&f);

  /** Expand the hash if needed.

      Useful primarily when the expansion policy is set to @c MANUAL.
   */
  void expand();

  /// Number of elements in the map.
  size_t count() const;

  /// Number of buckets in the array.
  size_t bucket_count() const;

  /// Set the expansion policy to @a policy.
  self_type &set_expansion_policy(ExpansionPolicy policy);

  /// Get the current expansion policy
  ExpansionPolicy get_expansion_policy() const;

  /// Set the limit value for the expansion policy.
  self_type &set_expansion_limit(size_t n);

  /// Set the limit value for the expansion policy.
  size_t get_expansion_limit() const;

protected:
  /// The type of storage for the buckets.
  using Table = std::vector<Bucket>;

  List _list;   ///< Elements in the table.
  Table _table; ///< Array of buckets.

  /// List of non-empty buckets.
  IntrusiveDList<typename Bucket::Linkage> _active_buckets;

  Bucket *bucket_for(key_type key);

  ExpansionPolicy _expansion_policy{DEFAULT_EXPANSION_POLICY}; ///< When to exand the table.
  size_t _expansion_limit{DEFAULT_EXPANSION_LIMIT};            ///< Limit value for expansion.

  // noncopyable
  IntrusiveHashMap(const IntrusiveHashMap &) = delete;
  IntrusiveHashMap &operator=(const IntrusiveHashMap &) = delete;

  // Hash table size prime list.
  static constexpr std::array<size_t, 29> PRIME = {{1,        3,        7,         13,        31,       61,      127,     251,
                                                    509,      1021,     2039,      4093,      8191,     16381,   32749,   65521,
                                                    131071,   262139,   524287,    1048573,   2097143,  4194301, 8388593, 16777213,
                                                    33554393, 67108859, 134217689, 268435399, 536870909}};
};

template <typename H>
auto
IntrusiveHashMap<H>::Bucket::Linkage::next_ptr(Bucket *b) -> Bucket *&
{
  return b->_link._next;
}

template <typename H>
auto
IntrusiveHashMap<H>::Bucket::Linkage::prev_ptr(Bucket *b) -> Bucket *&
{
  return b->_link._prev;
}

// This is designed so that if the bucket is empty, then @c nullptr is returned, which will immediately terminate
// a search loop on an empty bucket because that will start with a nullptr candidate, matching the limit.
template <typename H>
auto
IntrusiveHashMap<H>::Bucket::limit() const -> value_type *
{
  Bucket *n{_link._next};
  return n ? n->_v : nullptr;
};

template <typename H>
void
IntrusiveHashMap<H>::Bucket::clear()
{
  _v       = nullptr;
  _count   = 0;
  _mixed_p = false;
}

template <typename H>
bool
IntrusiveHashMap<H>::Bucket::contains(value_type *v) const
{
  value_type *x     = _v;
  value_type *limit = this->limit();
  while (x != limit && x != v) {
    x = H::next_ptr(x);
  }
  return x == v;
}

// ---------------------
template <typename H>
auto
IntrusiveHashMap<H>::range::begin() const -> iterator const &
{
  return super_type::first;
}
template <typename H>
auto
IntrusiveHashMap<H>::range::end() const -> iterator const &
{
  return super_type::second;
}

template <typename H>
auto
IntrusiveHashMap<H>::const_range::begin() const -> const_iterator const &
{
  return super_type::first;
}

template <typename H>
auto
IntrusiveHashMap<H>::const_range::end() const -> const_iterator const &
{
  return super_type::second;
}

// ---------------------

template <typename H> IntrusiveHashMap<H>::IntrusiveHashMap(size_t n)
{
  if (n) {
    _table.resize(*std::lower_bound(PRIME.begin(), PRIME.end(), n));
  }
}

template <typename H>
auto
IntrusiveHashMap<H>::bucket_for(key_type key) -> Bucket *
{
  return &_table[H::hash_of(key) % _table.size()];
}

template <typename H>
auto
IntrusiveHashMap<H>::begin() -> iterator
{
  return _list.begin();
}

template <typename H>
auto
IntrusiveHashMap<H>::begin() const -> const_iterator
{
  return _list.begin();
}

template <typename H>
auto
IntrusiveHashMap<H>::end() -> iterator
{
  return _list.end();
}

template <typename H>
auto
IntrusiveHashMap<H>::end() const -> const_iterator
{
  return _list.end();
}

template <typename H>
auto
IntrusiveHashMap<H>::clear() -> self_type &
{
  for (auto &b : _table) {
    b.clear();
  }
  // Clear container data.
  _list.clear();
  _active_buckets.clear();
  return *this;
}

template <typename H>
auto
IntrusiveHashMap<H>::find(key_type key) -> iterator
{
  Bucket *b         = this->bucket_for(key);
  value_type *v     = b->_v;
  value_type *limit = b->limit();
  while (v != limit && !H::equal(key, H::key_of(v))) {
    v = H::next_ptr(v);
  }
  return _list.iterator_for(v);
}

template <typename H>
auto
IntrusiveHashMap<H>::find(key_type key) const -> const_iterator
{
  return const_cast<self_type *>(this)->find(key);
}

template <typename H>
auto
IntrusiveHashMap<H>::equal_range(key_type key) -> range
{
  iterator first{this->find(key)};
  iterator last{first};
  iterator limit{this->end()};

  while (last != limit && H::equal(key, H::key_of(&*last))) {
    ++last;
  }

  return range{first, last};
}

template <typename H>
auto
IntrusiveHashMap<H>::equal_range(key_type key) const -> const_range
{
  return const_cast<self_type *>(this)->equal_range(key);
}

template <typename H>
auto
IntrusiveHashMap<H>::iterator_for(const value_type *v) const -> const_iterator
{
  return _list.iterator_for(v);
}

template <typename H>
auto
IntrusiveHashMap<H>::iterator_for(value_type *v) -> iterator
{
  return _list.iterator_for(v);
}

template <typename H>
auto
IntrusiveHashMap<H>::find(value_type *v) -> iterator
{
  Bucket *b = this->bucket_for(H::key_of(v));
  return b->contains(v) ? _list.iterator_for(v) : this->end();
}

template <typename H>
auto
IntrusiveHashMap<H>::find(value_type const *v) const -> const_iterator
{
  return const_cast<self_type *>(this)->find(const_cast<value_type *>(v));
}

template <typename H>
void
IntrusiveHashMap<H>::insert(value_type *v)
{
  auto key         = H::key_of(v);
  Bucket *bucket   = this->bucket_for(key);
  value_type *spot = bucket->_v;

  if (nullptr == spot) { // currently empty bucket, set it and add to active list.
    _list.append(v);
    bucket->_v = v;
    _active_buckets.append(bucket);
  } else {
    value_type *limit = bucket->limit();

    while (spot != limit && !H::equal(key, H::key_of(spot))) {
      spot = H::next_ptr(spot);
    }

    if (spot == limit) { // this key is not in the bucket, add it at the end and note this is now a mixed bucket.
      _list.insert_before(bucket->_v, v);
      bucket->_v       = v;
      bucket->_mixed_p = true;
    } else { // insert before the first matching key.
      _list.insert_before(spot, v);
      if (spot == bucket->_v) { // added before the bucket start, update the start.
        bucket->_v = v;
      } else { // if the matching key wasn't first, there is some other key in the bucket, mark it mixed.
        bucket->_mixed_p = true;
      }
    }
  }
  ++bucket->_count;

  // auto expand if appropriate.
  if ((AVERAGE == _expansion_policy && (_list.count() / _table.size()) > _expansion_limit) ||
      (MAXIMUM == _expansion_policy && bucket->_count > _expansion_limit && bucket->_mixed_p)) {
    this->expand();
  }
}

template <typename H>
auto
IntrusiveHashMap<H>::erase(iterator const &loc) -> iterator
{
  value_type *v     = loc;
  iterator zret     = ++(this->iterator_for(v)); // get around no const_iterator -> iterator.
  Bucket *b         = this->bucket_for(H::key_of(v));
  value_type *nv    = H::next_ptr(v);
  value_type *limit = b->limit();
  if (b->_v == v) {    // removed first element in bucket, update bucket
    if (limit == nv) { // that was also the only element, deactivate bucket
      _active_buckets.erase(b);
      b->clear();
    } else {
      b->_v = nv;
      --b->_count;
    }
  }
  _list.erase(loc);
  return zret;
}

template <typename H>
bool
IntrusiveHashMap<H>::erase(value_type *value)
{
  auto loc = this->find(value);
  if (loc != this->end()) {
    this->erase(loc);
    return true;
  }
  return false;
}

template <typename H>
auto
IntrusiveHashMap<H>::erase(iterator const &start, iterator const &limit) -> iterator
{
  auto spot{start};
  Bucket *bucket{this->bucket_for(spot)};
  while (spot != limit) {
    auto target         = bucket;
    bucket              = bucket->_link._next; // bump now to avoid forward iteration problems in case of bucket removal.
    value_type *v_limit = bucket ? bucket->_v : nullptr;
    while (spot != v_limit && spot != limit) {
      --(target->_count);
      ++spot;
    }
    if (target->_count == 0) {
      _active_buckets.erase(target);
    }
  }
  _list.erase(start, limit);
  return _list.iterator_for(limit); // convert from const_iterator back to iterator
};

template <typename H>
auto
IntrusiveHashMap<H>::erase(range const &r) -> iterator
{
  return this->erase(r.first, r.second);
}

template <typename H>
template <typename F>
auto
IntrusiveHashMap<H>::apply(F &&f) -> self_type &
{
  iterator spot{this->begin()};
  iterator limit{this->end()};
  while (spot != limit) {
    f(*spot++); // post increment means @a spot is updated before @a f is applied.
  }
  return *this;
};

template <typename H>
void
IntrusiveHashMap<H>::expand()
{
  ExpansionPolicy org_expansion_policy = _expansion_policy; // save for restore.
  value_type *old                      = _list.head();      // save for repopulating.
  auto old_size                        = _table.size();

  // Reset to empty state.
  this->clear();
  _table.resize(*std::lower_bound(PRIME.begin(), PRIME.end(), old_size + 1));

  _expansion_policy = MANUAL; // disable any auto expand while we're expanding.
  while (old) {
    value_type *v = old;
    old           = H::next_ptr(old);
    this->insert(v);
  }
  // stashed array gets cleaned up when @a tmp goes out of scope.
  _expansion_policy = org_expansion_policy; // reset to original value.
}

template <typename H>
size_t
IntrusiveHashMap<H>::count() const
{
  return _list.count();
}

template <typename H>
size_t
IntrusiveHashMap<H>::bucket_count() const
{
  return _table.size();
}

template <typename H>
auto
IntrusiveHashMap<H>::set_expansion_policy(ExpansionPolicy policy) -> self_type &
{
  _expansion_policy = policy;
  return *this;
}

template <typename H>
auto
IntrusiveHashMap<H>::get_expansion_policy() const -> ExpansionPolicy
{
  return _expansion_policy;
}

template <typename H>
auto
IntrusiveHashMap<H>::set_expansion_limit(size_t n) -> self_type &
{
  _expansion_limit = n;
  return *this;
}

template <typename H>
size_t
IntrusiveHashMap<H>::get_expansion_limit() const
{
  return _expansion_limit;
}
/* ---------------------------------------------------------------------------------------------- */
