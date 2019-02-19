/** @file
    IP address map support.

    Provide the ability to create a range based mapping for the IP
    address space. Addresses can be added and removed and each address
    is associated with arbitrary client data.

    @internal Don't bother to look at this code if you don't know how
    a red/black tree works. There are so many good references on the
    subject it's a waste to have some inferior version here. The
    methods on @c Node follow the standard implementation except for
    being parameterized by direction (so that, for instance, right
    rotate and left rotate are both done by the @c rotate method with
    a direction argument).

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

// Validation / printing disabled until I figure out how to generalize so
// as to not tie reporting into a particular project environment.

/* @internal It is a bit ugly to store a @c sockaddr equivalent in the table
    as all that is actually needed is the raw address. Unfortunately some clients
    require a @c sockaddr* return via the iterator and that's expensive to
    compute all the time. I should, at some point, re-examine this and see if we
    can do better and have a more compact internal format. I suspect I did this
    before we had IpAddr as a type.
*/

#include "tscore/IpMap.h"
#include "tscore/ink_inet.h"

namespace ts
{
namespace detail
{
  // Helper functions

  inline int
  cmp(sockaddr_in6 const &lhs, sockaddr_in6 const &rhs)
  {
    return memcmp(lhs.sin6_addr.s6_addr, rhs.sin6_addr.s6_addr, TS_IP6_SIZE);
  }

  /// Less than.
  inline bool
  operator<(sockaddr_in6 const &lhs, sockaddr_in6 const &rhs)
  {
    return ts::detail::cmp(lhs, rhs) < 0;
  }
  inline bool
  operator<(sockaddr_in6 const *lhs, sockaddr_in6 const &rhs)
  {
    return ts::detail::cmp(*lhs, rhs) < 0;
  }
  /// Less than.
  inline bool
  operator<(sockaddr_in6 const &lhs, sockaddr_in6 const *rhs)
  {
    return ts::detail::cmp(lhs, *rhs) < 0;
  }
  /// Equality.
  inline bool
  operator==(sockaddr_in6 const &lhs, sockaddr_in6 const *rhs)
  {
    return ts::detail::cmp(lhs, *rhs) == 0;
  }
  /// Equality.
  inline bool
  operator==(sockaddr_in6 const *lhs, sockaddr_in6 const &rhs)
  {
    return ts::detail::cmp(*lhs, rhs) == 0;
  }
  /// Equality.
  inline bool
  operator==(sockaddr_in6 const &lhs, sockaddr_in6 const &rhs)
  {
    return ts::detail::cmp(lhs, rhs) == 0;
  }
  /// Less than or equal.
  inline bool
  operator<=(sockaddr_in6 const &lhs, sockaddr_in6 const *rhs)
  {
    return ts::detail::cmp(lhs, *rhs) <= 0;
  }
  /// Less than or equal.
  inline bool
  operator<=(sockaddr_in6 const &lhs, sockaddr_in6 const &rhs)
  {
    return ts::detail::cmp(lhs, rhs) <= 0;
  }
  /// Greater than or equal.
  inline bool
  operator>=(sockaddr_in6 const &lhs, sockaddr_in6 const &rhs)
  {
    return ts::detail::cmp(lhs, rhs) >= 0;
  }
  /// Greater than or equal.
  inline bool
  operator>=(sockaddr_in6 const &lhs, sockaddr_in6 const *rhs)
  {
    return ts::detail::cmp(lhs, *rhs) >= 0;
  }
  /// Greater than.
  inline bool
  operator>(sockaddr_in6 const &lhs, sockaddr_in6 const *rhs)
  {
    return ts::detail::cmp(lhs, *rhs) > 0;
  }

  /** Base template class for IP maps.
      This class is templated by the @a N type which must be a subclass
      of @c RBNode. This class carries information about the addresses stored
      in the map. This includes the type, the common argument type, and
      some utility methods to operate on the address.
  */
  template <typename N ///< Node type.
            >
  struct IpMapBase {
    friend class ::IpMap;

    using self_type = IpMapBase<N>;        ///< Self reference type.
    using ArgType   = typename N::ArgType; ///< Import type.
    using Metric    = typename N::Metric;  ///< Import type.g482

    IpMapBase() = default;
    IpMapBase(self_type &&that) : _root(that._root), _list(std::move(that._list)) { that._root = nullptr; }
    ~IpMapBase();
    /** Mark a range.
        All addresses in the range [ @a min , @a max ] are marked with @a data.
        @return This object.
    */
    self_type &mark(ArgType min,         ///< Minimum value in range.
                    ArgType max,         ///< Maximum value in range.
                    void *data = nullptr ///< Client data payload.
    );
    /** Unmark addresses.

        All addresses in the range [ @a min , @a max ] are cleared
        (removed from the map), no longer marked.

        @return This object.
    */
    self_type &unmark(ArgType min, ArgType max);

    /** Fill addresses.

        This background fills using the range. All addresses in the
        range that are @b not present in the map are added. No
        previously present address is changed.

        @note This is useful for filling in first match tables.

        @return This object.
    */
    self_type &fill(ArgType min, ArgType max, void *data = nullptr);

    /** Test for membership.

        @return @c true if the address is in the map, @c false if not.
        If the address is in the map and @a ptr is not @c nullptr, @c *ptr
        is set to the client data for the address.
    */
    bool contains(ArgType target,      ///< Search target value.
                  void **ptr = nullptr ///< Client data return.
                  ) const;

    /** Remove all addresses in the map.

        @note This is much faster than using @c unmark with a range of
        all addresses.

        @return This object.
    */
    self_type &clear();

    /** Lower bound for @a target.  @return The node whose minimum value
        is the largest that is not greater than @a target, or @c nullptr if
        all minimum values are larger than @a target.
    */
    N *lowerBound(ArgType target);

    /** Insert @a n after @a spot.
        Caller is responsible for ensuring that @a spot is in this container
        and the proper location for @a n.
    */
    void insert_after(N *spot, ///< Node in list.
                      N *n     ///< Node to insert.
    );
    /** Insert @a n before @a spot.
        Caller is responsible for ensuring that @a spot is in this container
        and the proper location for @a n.
    */
    void insert_before(N *spot, ///< Node in list.
                       N *n     ///< Node to insert.
    );
    /// Add node @a n as the first node.
    void prepend(N *n);
    /// Add node @a n as the last node.
    void append(N *n);
    /// Remove a node.
    void remove(N *n ///< Node to remove.
    );

    /** Validate internal data structures.
        @note Intended for debugging, not general client use.
    */
    void validate();

    /// @return The number of distinct ranges.
    size_t count() const;

    /// Print all spans.
    /// @return This map.
    self_type &print();

    // Helper methods.
    N *
    prev(RBNode *n) const
    {
      return static_cast<N *>(n->_prev);
    }
    N *
    next(RBNode *n) const
    {
      return static_cast<N *>(n->_next);
    }
    N *
    parent(RBNode *n) const
    {
      return static_cast<N *>(n->_parent);
    }
    N *
    left(RBNode *n) const
    {
      return static_cast<N *>(n->_left);
    }
    N *
    right(RBNode *n) const
    {
      return static_cast<N *>(n->_right);
    }
    N *
    head()
    {
      return static_cast<N *>(_list.head());
    }
    N *
    tail()
    {
      return static_cast<N *>(_list.tail());
    }

    N *_root = nullptr; ///< Root node.
    /// In order list of nodes.
    /// For ugly compiler reasons, this is a list of base class pointers
    /// even though we really store @a N instances on it.
    struct NodeLinkage {
      static RBNode *&
      next_ptr(RBNode *n)
      {
        return n->_next;
      }
      static RBNode *&
      prev_ptr(RBNode *n)
      {
        return n->_prev;
      }
    };
    using NodeList = ts::IntrusiveDList<NodeLinkage>;
    /// This keeps track of all allocated nodes in order.
    /// Iteration depends on this list being maintained.
    NodeList _list;
  };

  template <typename N>
  N *
  IpMapBase<N>::lowerBound(ArgType target)
  {
    N *n    = _root;   // current node to test.
    N *zret = nullptr; // best node so far.
    while (n) {
      if (target < n->_min) {
        n = left(n);
      } else {
        zret = n; // this is a better candidate.
        if (n->_max < target) {
          n = right(n);
        } else {
          break;
        }
      }
    }
    return zret;
  }

  template <typename N>
  IpMapBase<N> &
  IpMapBase<N>::clear()
  {
    // Delete everything.
    N *n = static_cast<N *>(_list.head());
    while (n) {
      N *x = n;
      n    = next(n);
      delete x;
    }
    _list.clear();
    _root = nullptr;
    return *this;
  }

  template <typename N>
  IpMapBase<N> &
  IpMapBase<N>::fill(ArgType rmin, ArgType rmax, void *payload)
  {
    // Rightmost node of interest with n->_min <= min.
    N *n = this->lowerBound(rmin);
    N *x = nullptr; // New node (if any).
    // Need copies because we will modify these.
    Metric min = N::deref(rmin);
    Metric max = N::deref(rmax);

    // Handle cases involving a node of interest to the left of the
    // range.
    if (n) {
      if (n->_min < min) {
        Metric min_1 = min;
        N::dec(min_1);         // dec is OK because min isn't zero.
        if (n->_max < min_1) { // no overlap or adj.
          n = next(n);
        } else if (n->_max >= max) { // incoming range is covered, just discard.
          return *this;
        } else if (n->_data != payload) { // different payload, clip range on left.
          min = n->_max;
          N::inc(min);
          n = next(n);
        } else { // skew overlap with same payload, use node and continue.
          x = n;
          n = next(n);
        }
      }
    } else {
      n = this->head();
    }

    // Work through the rest of the nodes of interest.
    // Invariant: n->_min >= min

    // Careful here -- because max_plus1 might wrap we need to use it only
    // if we can certain it didn't. This is done by ordering the range
    // tests so that when max_plus1 is used when we know there exists a
    // larger value than max.
    Metric max_plus1 = max;
    N::inc(max_plus1);
    /* Notes:
       - max (and thence max_plus1) never change during the loop.
       - we must have either x != 0 or adjust min but not both.
    */
    while (n) {
      if (n->_data == payload) {
        if (x) {
          if (n->_max <= max) {
            // next range is covered, so we can remove and continue.
            this->remove(n);
            n = next(x);
          } else if (n->_min <= max_plus1) {
            // Overlap or adjacent with larger max - absorb and finish.
            x->setMax(n->_max);
            this->remove(n);
            return *this;
          } else {
            // have the space to finish off the range.
            x->setMax(max);
            return *this;
          }
        } else {                // not carrying a span.
          if (n->_max <= max) { // next range is covered - use it.
            x = n;
            x->setMin(min);
            n = next(n);
          } else if (n->_min <= max_plus1) {
            n->setMin(min);
            return *this;
          } else { // no overlap, space to complete range.
            this->insert_before(n, new N(min, max, payload));
            return *this;
          }
        }
      } else { // different payload
        if (x) {
          if (max < n->_min) { // range ends before n starts, done.
            x->setMax(max);
            return *this;
          } else if (max <= n->_max) { // range ends before n, done.
            x->setMaxMinusOne(n->_min);
            return *this;
          } else { // n is contained in range, skip over it.
            x->setMaxMinusOne(n->_min);
            x   = nullptr;
            min = n->_max;
            N::inc(min); // OK because n->_max maximal => next is null.
            n = next(n);
          }
        } else {               // no carry node.
          if (max < n->_min) { // entirely before next span.
            this->insert_before(n, new N(min, max, payload));
            return *this;
          } else {
            if (min < n->_min) { // leading section, need node.
              N *y = new N(min, n->_min, payload);
              y->decrementMax();
              this->insert_before(n, y);
            }
            if (max <= n->_max) { // nothing past node
              return *this;
            }
            min = n->_max;
            N::inc(min);
            n = next(n);
          }
        }
      }
    }
    // Invariant: min is larger than any existing range maximum.
    if (x) {
      x->setMax(max);
    } else {
      this->append(new N(min, max, payload));
    }
    return *this;
  }

  template <typename N>
  IpMapBase<N> &
  IpMapBase<N>::mark(ArgType min, ArgType max, void *payload)
  {
    N *n = this->lowerBound(min); // current node.
    N *x = nullptr;               // New node, gets set if we re-use an existing one.
    N *y = nullptr;               // Temporary for removing and advancing.

    // Several places it is handy to have max+1. Must be careful
    // about wrapping.
    Metric max_plus = N::deref(max);
    N::inc(max_plus);

    /* Some subtlety - for IPv6 we overload the compare operators to do the right thing, but we
     * can't overload pointer comparisons. Therefore we carefully never compare pointers in this
     * logic. Only @a min and @a max can be pointers, everything else is an instance or a reference.
     * Since there's no good reason to compare @a min and @a max this isn't particularly tricky, but
     * it's good to keep in mind. If we were somewhat more clever, we would provide static less than
     * and equal operators in the template class @a N and convert all the comparisons to use only
     * those two via static function call.
     */

    /*  We have lots of special cases here primarily to minimize memory
        allocation by re-using an existing node as often as possible.
    */
    if (n) {
      // Watch for wrap.
      Metric min_1 = N::deref(min);
      N::dec(min_1);
      if (n->_min == min) {
        // Could be another span further left which is adjacent.
        // Coalesce if the data is the same. min_1 is OK because
        // if there is a previous range, min is not zero.
        N *p = prev(n);
        if (p && p->_data == payload && p->_max == min_1) {
          x = p;
          n = x; // need to back up n because frame of reference moved.
          x->setMax(max);
        } else if (n->_max <= max) {
          // Span will be subsumed by request span so it's available for use.
          x = n;
          x->setMax(max).setData(payload);
        } else if (n->_data == payload) {
          return *this; // request is covered by existing span with the same data
        } else {
          // request span is covered by existing span.
          x = new N(min, max, payload); //
          n->setMin(max_plus);          // clip existing.
          this->insert_before(n, x);
          return *this;
        }
      } else if (n->_data == payload && n->_max >= min_1) {
        // min_1 is safe here because n->_min < min so min is not zero.
        x = n;
        // If the existing span covers the requested span, we're done.
        if (x->_max >= max) {
          return *this;
        }
        x->setMax(max);
      } else if (n->_max <= max) {
        // Can only have left skew overlap, otherwise disjoint.
        // Clip if overlap.
        if (n->_max >= min) {
          n->setMax(min_1);
        } else if (nullptr != (y = next(n)) && y->_max <= max) {
          // because @a n was selected as the minimum it must be the case that
          // y->min >= min (or y would have been selected). Therefore in this
          // case the request covers the next node therefore it can be reused.
          x = y;
          x->setMin(min).setMax(max).setData(payload);
          n = x; // this gets bumped again, which is correct.
        }
      } else {
        // Existing span covers new span but with a different payload.
        // We split it, put the new span in between and we're done.
        // max_plus is valid because n->_max > max.
        N *r;
        x = new N(min, max, payload);
        r = new N(max_plus, n->_max, n->_data);
        n->setMax(min_1);
        this->insert_after(n, x);
        this->insert_after(x, r);
        return *this; // done.
      }
      n = next(n); // lower bound span handled, move on.
      if (!x) {
        x = new N(min, max, payload);
        if (n) {
          this->insert_before(n, x);
        } else {
          this->append(x); // note that since n == 0 we'll just return.
        }
      }
    } else if (nullptr != (n = this->head()) &&        // at least one node in tree.
               n->_data == payload &&                  // payload matches
               (n->_max <= max || n->_min <= max_plus) // overlap or adj.
    ) {
      // Same payload with overlap, re-use.
      x = n;
      n = next(n);
      x->setMin(min);
      if (x->_max < max) {
        x->setMax(max);
      }
    } else {
      x = new N(min, max, payload);
      this->prepend(x);
    }

    // At this point, @a x has the node for this span and all existing spans of
    // interest start at or past this span.
    while (n) {
      if (n->_max <= max) { // completely covered, drop span, continue
        y = n;
        n = next(n);
        this->remove(y);
      } else if (max_plus < n->_min) { // no overlap, done.
        break;
      } else if (n->_data == payload) { // skew overlap or adj., same payload
        x->setMax(n->_max);
        y = n;
        n = next(n);
        this->remove(y);
      } else if (n->_min <= max) { // skew overlap different payload
        n->setMin(max_plus);
        break;
      }
    }

    return *this;
  }

  template <typename N>
  IpMapBase<N> &
  IpMapBase<N>::unmark(ArgType min, ArgType max)
  {
    N *n = this->lowerBound(min);
    N *x; // temp for deletes.

    // Need to handle special case where first span starts to the left.
    if (n && n->_min < min) {
      if (n->_max >= min) { // some overlap
        if (n->_max > max) {
          // request span is covered by existing span - split existing span.
          x = new N(max, N::argue(n->_max), n->_data);
          x->incrementMin();
          n->setMaxMinusOne(N::deref(min));
          this->insert_after(n, x);
          return *this; // done.
        } else {
          n->setMaxMinusOne(N::deref(min)); // just clip overlap.
        }
      } // else disjoint so just skip it.
      n = next(n);
    }
    // n and all subsequent spans start at >= min.
    while (n) {
      x = n;
      n = next(n);
      if (x->_max <= max) {
        this->remove(x);
      } else {
        if (x->_min <= max) { // clip overlap
          x->setMinPlusOne(N::deref(max));
        }
        break;
      }
    }
    return *this;
  }

  template <typename N>
  void
  IpMapBase<N>::insert_after(N *spot, N *n)
  {
    N *c = right(spot);
    if (!c) {
      spot->setChild(n, N::RIGHT);
    } else {
      spot->_next->setChild(n, N::LEFT);
    }

    _list.insert_after(spot, n);
    _root = static_cast<N *>(n->rebalanceAfterInsert());
  }

  template <typename N>
  void
  IpMapBase<N>::insert_before(N *spot, N *n)
  {
    N *c = left(spot);
    if (!c) {
      spot->setChild(n, N::LEFT);
    } else {
      spot->_prev->setChild(n, N::RIGHT);
    }

    _list.insert_before(spot, n);
    _root = static_cast<N *>(n->rebalanceAfterInsert());
  }

  template <typename N>
  void
  IpMapBase<N>::prepend(N *n)
  {
    if (!_root) {
      _root = n;
    } else {
      _root = static_cast<N *>(_list.head()->setChild(n, N::LEFT)->rebalanceAfterInsert());
    }
    _list.prepend(n);
  }

  template <typename N>
  void
  IpMapBase<N>::append(N *n)
  {
    if (!_root) {
      _root = n;
    } else {
      _root = static_cast<N *>(_list.tail()->setChild(n, N::RIGHT)->rebalanceAfterInsert());
    }
    _list.append(n);
  }

  template <typename N>
  void
  IpMapBase<N>::remove(N *n)
  {
    _root = static_cast<N *>(n->remove());
    _list.erase(n);
    delete n;
  }

  template <typename N>
  bool
  IpMapBase<N>::contains(ArgType x, void **ptr) const
  {
    bool zret = false;
    N *n      = _root; // current node to test.
    while (n) {
      if (x < n->_min) {
        n = left(n);
      } else if (n->_max < x) {
        n = right(n);
      } else {
        if (ptr) {
          *ptr = n->_data;
        }
        zret = true;
        break;
      }
    }
    return zret;
  }

  template <typename N>
  size_t
  IpMapBase<N>::count() const
  {
    return _list.count();
  }
  //----------------------------------------------------------------------------
  template <typename N>
  void
  IpMapBase<N>::validate()
  {
#if 0
  if (_root) _root->validate();
  for ( Node* n = _list.head() ; n ; n = n->_next ) {
    Node* x;
    if (0 != (x = n->_next)) {
      if (x->_prev != n)
        std::cout << "Broken list" << std::endl;
      if (n->_max >= x->_min)
        std::cout << "Out of order - " << n->_max << " > " << x->_min << std::endl;
      if (n->_parent == n || n->_left == n || n->_right == n)
        std::cout << "Looped node" << std::endl;
    }
  }
#endif
  }

  template <typename N>
  IpMapBase<N> &
  IpMapBase<N>::print()
  {
#if 0
  for ( Node* n = _list.head() ; n ; n = n->_next ) {
    std::cout
      << n << ": " << n->_min << '-' << n->_max << " [" << n->_data << "] "
      << (n->_color == Node::BLACK ? "Black " : "Red   ") << "P=" << n->_parent << " L=" << n->_left << " R=" << n->_right
      << std::endl;
  }
#endif
    return *this;
  }

  template <typename N> IpMapBase<N>::~IpMapBase() { this->clear(); }

  //----------------------------------------------------------------------------
  using Ip4Span = Interval<in_addr_t, in_addr_t>;

  /** Node for IPv4 map.
      We store the address in host order in the @a _min and @a _max
      members for performance. We store copies in the @a _sa member
      for API compliance (which requires @c sockaddr* access).
  */
  class Ip4Node : public IpMap::Node, protected Ip4Span
  {
    friend struct IpMapBase<Ip4Node>;

  public:
    using self_type = ts::detail::Ip4Node; ///< Self reference type.

    /// Construct with values.
    Ip4Node(ArgType min, ///< Minimum address (host order).
            ArgType max, ///< Maximum address (host order).
            void *data   ///< Client data.
            )
      : Node(data), Ip4Span(min, max), _sa()
    {
      ats_ip4_set(ats_ip_sa_cast(&_sa._min), htonl(min));
      ats_ip4_set(ats_ip_sa_cast(&_sa._max), htonl(max));
    }
    /// @return The minimum value of the interval.
    sockaddr const *
    min() const override
    {
      return ats_ip_sa_cast(&_sa._min);
    }
    /// @return The maximum value of the interval.
    sockaddr const *
    max() const override
    {
      return ats_ip_sa_cast(&_sa._max);
    }
    /// Set the client data.
    self_type &
    setData(void *data ///< Client data.
            ) override
    {
      _data = data;
      return *this;
    }

  protected:
    /// Set the minimum value of the interval.
    /// @return This interval.
    self_type &
    setMin(ArgType min ///< Minimum value (host order).
    )
    {
      _min                     = min;
      _sa._min.sin_addr.s_addr = htonl(min);
      return *this;
    }

    /// Set the maximum value of the interval.
    /// @return This interval.
    self_type &
    setMax(ArgType max ///< Maximum value (host order).
    )
    {
      _max                     = max;
      _sa._max.sin_addr.s_addr = htonl(max);
      return *this;
    }

    /** Set the maximum value to one less than @a max.
        @return This object.
    */
    self_type &
    setMaxMinusOne(ArgType max ///< One more than maximum value.
    )
    {
      return this->setMax(max - 1);
    }
    /** Set the minimum value to one more than @a min.
        @return This object.
    */
    self_type &
    setMinPlusOne(ArgType min ///< One less than minimum value.
    )
    {
      return this->setMin(min + 1);
    }
    /** Decremement the maximum value in place.
        @return This object.
    */
    self_type &
    decrementMax()
    {
      this->setMax(_max - 1);
      return *this;
    }
    /** Increment the minimum value in place.
        @return This object.
    */
    self_type &
    incrementMin()
    {
      this->setMin(_min + 1);
      return *this;
    }

    /// Increment a metric.
    static void
    inc(Metric &m ///< Incremented in place.
    )
    {
      ++m;
    }

    /// Decrement a metric.
    static void
    dec(Metric &m ///< Decremented in place.
    )
    {
      --m;
    }

    /// @return Dereferenced @a addr.
    static Metric
    deref(ArgType addr ///< Argument to dereference.
    )
    {
      return addr;
    }

    /// @return The argument type for the @a metric.
    static ArgType
    argue(Metric const &metric)
    {
      return metric;
    }

    struct {
      sockaddr_in _min;
      sockaddr_in _max;
    } _sa; ///< Addresses in API compliant form.
  };

  class Ip4Map : public IpMapBase<Ip4Node>
  {
    friend class ::IpMap;
  };

  //----------------------------------------------------------------------------
  using Ip6Span = Interval<sockaddr_in6>;

  /** Node for IPv6 map.
   */
  class Ip6Node : public IpMap::Node, protected Ip6Span
  {
    friend struct IpMapBase<Ip6Node>;

  public:
    using self_type = ts::detail::Ip6Node; ///< Self reference type.
    /// Override @c ArgType from @c Interval because the convention
    /// is to use a pointer, not a reference.
    using ArgType = const ts::detail::Interval<sockaddr_in6, const sockaddr_in6 &>::Metric *;

    /** Construct from the argument type.
     *
     * @param min Minimum value in the range.
     * @param max Maximum value in the range (inclusvie).
     * @param data Data to attach to the range.
     */

    Ip6Node(ArgType min, ///< Minimum address (network order).
            ArgType max, ///< Maximum address (network order).
            void *data   ///< Client data.
            )
      : Node(data), Ip6Span(*min, *max)
    {
    }

    /** Construct from the underlying @c Metric type @a min to @a max
     *
     * @param min Minimum value in the range.
     * @param max Maximum value in the range (inclusvie).
     * @param data Data to attach to the range.
     */
    Ip6Node(Metric const &min, Metric const &max, void *data) : Node(data), Ip6Span(min, max) {}

    /// @return The minimum value of the interval.
    sockaddr const *
    min() const override
    {
      return ats_ip_sa_cast(&_min);
    }

    /// @return The maximum value of the interval.
    sockaddr const *
    max() const override
    {
      return ats_ip_sa_cast(&_max);
    }

    /** Set the client @a data.
     *
     * @param data Client data.
     * @return @a this
     */
    self_type &
    setData(void *data) override
    {
      _data = data;
      return *this;
    }

  protected:
    /// Set the minimum value of the interval.
    /// @return This interval.
    self_type &
    setMin(ArgType min ///< Minimum value (host order).
    )
    {
      ats_ip_copy(ats_ip_sa_cast(&_min), ats_ip_sa_cast(min));
      return *this;
    }

    /// Set the minimum value of the interval.
    /// @note Convenience overload.
    /// @return This interval.
    self_type &
    setMin(Metric const &min ///< Minimum value (host order).
    )
    {
      return this->setMin(&min);
    }

    /// Set the maximum value of the interval.
    /// @return This interval.
    self_type &
    setMax(ArgType max ///< Maximum value (host order).
    )
    {
      ats_ip_copy(ats_ip_sa_cast(&_max), ats_ip_sa_cast(max));
      return *this;
    }
    /// Set the maximum value of the interval.
    /// @note Convenience overload.
    /// @return This interval.
    self_type &
    setMax(Metric const &max ///< Maximum value (host order).
    )
    {
      return this->setMax(&max);
    }
    /** Set the maximum value to one less than @a max.
        @return This object.
    */
    self_type &
    setMaxMinusOne(Metric const &max ///< One more than maximum value.
    )
    {
      this->setMax(max);
      dec(_max);
      return *this;
    }
    /** Set the minimum value to one more than @a min.
        @return This object.
    */
    self_type &
    setMinPlusOne(Metric const &min ///< One less than minimum value.
    )
    {
      this->setMin(min);
      inc(_min);
      return *this;
    }
    /** Decremement the maximum value in place.
        @return This object.
    */
    self_type &
    decrementMax()
    {
      dec(_max);
      return *this;
    }
    /** Increment the mininimum value in place.
        @return This object.
    */
    self_type &
    incrementMin()
    {
      inc(_min);
      return *this;
    }

    /// Increment a metric.
    static void
    inc(Metric &m ///< Incremented in place.
    )
    {
      uint8_t *addr = m.sin6_addr.s6_addr;
      uint8_t *b    = addr + TS_IP6_SIZE;
      // Ripple carry. Walk up the address incrementing until we don't
      // have a carry.
      do {
        ++*--b;
      } while (b > addr && 0 == *b);
    }

    /// Decrement a metric.
    static void
    dec(Metric &m ///< Decremented in place.
    )
    {
      uint8_t *addr = m.sin6_addr.s6_addr;
      uint8_t *b    = addr + TS_IP6_SIZE;
      // Ripple borrow. Walk up the address decrementing until we don't
      // have a borrow.
      do {
        --*--b;
      } while (b > addr && static_cast<uint8_t>(0xFF) == *b);
    }
    /// @return Dereferenced @a addr.
    static Metric const &
    deref(ArgType addr ///< Argument to dereference.
    )
    {
      return *addr;
    }

    /// @return The argument type for the @a metric.
    static ArgType
    argue(Metric const &metric)
    {
      return &metric;
    }
  };

  // We declare this after the helper operators and inside this namespace
  // so that the template uses these for comparisons.

  class Ip6Map : public IpMapBase<Ip6Node>
  {
    friend class ::IpMap;
  };
} // namespace detail
} // namespace ts
//----------------------------------------------------------------------------
IpMap::IpMap(IpMap::self_type &&that) noexcept : _m4(that._m4), _m6(that._m6)
{
  that._m4 = nullptr;
  that._m6 = nullptr;
}

IpMap::self_type &
IpMap::operator=(IpMap::self_type &&that)
{
  if (&that != this) {
    this->clear();
    std::swap(_m4, that._m4);
    std::swap(_m6, that._m6);
  }
  return *this;
}

IpMap::~IpMap()
{
  delete _m4;
  delete _m6;
}

inline ts::detail::Ip4Map *
IpMap::force4()
{
  if (!_m4) {
    _m4 = new ts::detail::Ip4Map;
  }
  return _m4;
}

inline ts::detail::Ip6Map *
IpMap::force6()
{
  if (!_m6) {
    _m6 = new ts::detail::Ip6Map;
  }
  return _m6;
}

bool
IpMap::contains(sockaddr const *target, void **ptr) const
{
  bool zret = false;
  if (AF_INET == target->sa_family) {
    zret = _m4 && _m4->contains(ntohl(ats_ip4_addr_cast(target)), ptr);
  } else if (AF_INET6 == target->sa_family) {
    zret = _m6 && _m6->contains(ats_ip6_cast(target), ptr);
  }
  return zret;
}

bool
IpMap::contains(in_addr_t target, void **ptr) const
{
  return _m4 && _m4->contains(ntohl(target), ptr);
}

IpMap &
IpMap::mark(sockaddr const *min, sockaddr const *max, void *data)
{
  ink_assert(min->sa_family == max->sa_family);
  if (AF_INET == min->sa_family) {
    this->force4()->mark(ntohl(ats_ip4_addr_cast(min)), ntohl(ats_ip4_addr_cast(max)), data);
  } else if (AF_INET6 == min->sa_family) {
    this->force6()->mark(ats_ip6_cast(min), ats_ip6_cast(max), data);
  }
  return *this;
}

IpMap &
IpMap::mark(in_addr_t min, in_addr_t max, void *data)
{
  this->force4()->mark(ntohl(min), ntohl(max), data);
  return *this;
}

IpMap &
IpMap::unmark(sockaddr const *min, sockaddr const *max)
{
  ink_assert(min->sa_family == max->sa_family);
  if (AF_INET == min->sa_family) {
    if (_m4) {
      _m4->unmark(ntohl(ats_ip4_addr_cast(min)), ntohl(ats_ip4_addr_cast(max)));
    }
  } else if (AF_INET6 == min->sa_family) {
    if (_m6) {
      _m6->unmark(ats_ip6_cast(min), ats_ip6_cast(max));
    }
  }
  return *this;
}

IpMap &
IpMap::unmark(in_addr_t min, in_addr_t max)
{
  if (_m4) {
    _m4->unmark(ntohl(min), ntohl(max));
  }
  return *this;
}

IpMap &
IpMap::fill(sockaddr const *min, sockaddr const *max, void *data)
{
  ink_assert(min->sa_family == max->sa_family);
  if (AF_INET == min->sa_family) {
    this->force4()->fill(ntohl(ats_ip4_addr_cast(min)), ntohl(ats_ip4_addr_cast(max)), data);
  } else if (AF_INET6 == min->sa_family) {
    this->force6()->fill(ats_ip6_cast(min), ats_ip6_cast(max), data);
  }
  return *this;
}

IpMap &
IpMap::fill(in_addr_t min, in_addr_t max, void *data)
{
  this->force4()->fill(ntohl(min), ntohl(max), data);
  return *this;
}

size_t
IpMap::count() const
{
  size_t zret = 0;
  if (_m4) {
    zret += _m4->count();
  }
  if (_m6) {
    zret += _m6->count();
  }
  return zret;
}

IpMap &
IpMap::clear()
{
  if (_m4) {
    _m4->clear();
  }
  if (_m6) {
    _m6->clear();
  }
  return *this;
}

IpMap::iterator
IpMap::begin() const
{
  Node *x = nullptr;
  if (_m4) {
    x = _m4->head();
  }
  if (!x && _m6) {
    x = _m6->head();
  }
  return iterator(this, x);
}

IpMap::iterator &
IpMap::iterator::operator++()
{
  if (_node) {
    // If we go past the end of the list see if it was the v4 list
    // and if so, move to the v6 list (if it's there).
    Node *x = static_cast<Node *>(_node->_next);
    if (!x && _tree->_m4 && _tree->_m6 && _node == _tree->_m4->tail()) {
      x = _tree->_m6->head();
    }
    _node = x;
  }
  return *this;
}

inline IpMap::iterator &
IpMap::iterator::operator--()
{
  if (_node) {
    // At a node, try to back up. Handle the case where we back over the
    // start of the v6 addresses and switch to the v4, if there are any.
    Node *x = static_cast<Node *>(_node->_prev);
    if (!x && _tree->_m4 && _tree->_m6 && _node == _tree->_m6->head()) {
      x = _tree->_m4->tail();
    }
    _node = x;
  } else if (_tree) {
    // We were at the end. Back up to v6 if possible, v4 if not.
    if (_tree->_m6) {
      _node = _tree->_m6->tail();
    }
    if (!_node && _tree->_m4) {
      _node = _tree->_m4->tail();
    }
  }
  return *this;
}

//----------------------------------------------------------------------------
//----------------------------------------------------------------------------
