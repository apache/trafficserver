/** @file

    Map of IP addresses to client data.

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

#include <algorithm>
#include <utility>

#include "tscore/ink_platform.h"
#include "tscore/ink_defs.h"
#include "tscore/RbTree.h"
#include "tscore/ink_inet.h"
#include "tscpp/util/IntrusiveDList.h"
#include "tscore/ink_assert.h"
#include "tscore/BufferWriterForward.h"

namespace ts
{
namespace detail
{
  /** Interval class.
      This holds an interval based on a metric @a T along with
      client data.
  */
  template <typename T,            ///< Metric for span.
            typename A = T const & ///< Argument type.
            >
  struct Interval {
    using Metric  = T; ///< Metric (storage) type.
    using ArgType = A; ///< Type used to pass instances of @c Metric.

    Interval() {} ///< Default constructor.
    /// Construct with values.
    Interval(ArgType min, ///< Minimum value in span.
             ArgType max  ///< Maximum value in span.
             )
      : _min(min), _max(max)
    {
    }
    Metric _min; ///< Minimum value in span.
    Metric _max; ///< Maximum value in span.
  };

  class Ip4Map; // Forward declare.
  class Ip6Map; // Forward declare.
} // namespace detail
} // namespace ts

/** Map from IP addresses to client data.

    Conceptually this class maps the entire space of IP addresses to
    client data. Client data is stored as a @c (void*). Memory
    management of the data is left to the client. The interface
    supports marking ranges of addresses with a specific client
    data. Marking takes a painter's algorithm approach -- any marking
    overwrites any previous marking on an address. Details of marking
    calls are discarded and only the final results are kept. That is,
    a client cannot unmark expliticly any previous marking. Only a
    specific range of addresses can be unmarked.

    Both IPv4 and IPv6 are supported in the same map. Mixed ranges are
    not supported (any particular range of addresses must be a single
    protocol but ranges of both types can be in the map).

    Use @c mark to mark / set / add addresses to the map.
    Use @c unmark to unset addresses (setting the client data to 0 does
    @b not remove the address -- this is for the convenience of clients
    that do not need data, only membership). @c contains tests for
    membership and retrieves the client data.

    Ranges can be marked and unmarked arbitrarily. The internal
    representation keeps a minimal set of ranges to describe the
    current addresses. Search time is O(log n) where n is the number
    of disjoint ranges. Marking and unmarking can take O(log n) and
    may require memory allocation / deallocation although this is
    minimized.
*/

class IpMap
{
public:
  using self_type = IpMap; ///< Self reference type.

  class iterator; // forward declare.

  static constexpr in_addr_t RAW_IP4_MIN_ADDR = 0;
  static constexpr IpAddr IP4_MIN_ADDR{RAW_IP4_MIN_ADDR};
  static constexpr in_addr_t RAW_IP4_MAX_ADDR = ~0;
  static constexpr IpAddr IP4_MAX_ADDR{RAW_IP4_MAX_ADDR};

  static constexpr in6_addr RAW_IP6_MIN_ADDR = {{{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}}};
  static constexpr IpAddr IP6_MIN_ADDR{RAW_IP6_MIN_ADDR};
  static constexpr in6_addr RAW_IP6_MAX_ADDR = {{{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}}};
  static constexpr IpAddr IP6_MAX_ADDR{RAW_IP6_MAX_ADDR};

  /** Public API for intervals in the map.
   */
  class Node : protected ts::detail::RBNode
  {
    friend class iterator;
    friend class IpMap;

  public:
    using self_type = Node; ///< Self reference type.
    /// Default constructor.
    Node() {}
    /// Construct with @a data.
    Node(void *data) : _data(data) {}
    /// @return Client data for the node.
    virtual void *
    data()
    {
      return _data;
    }
    /// Set client data.
    virtual self_type &
    setData(void *data ///< Client data pointer to store.
    )
    {
      _data = data;
      return *this;
    }
    /// @return Minimum value of the interval.
    virtual sockaddr const *min() const = 0;
    /// @return Maximum value of the interval.
    virtual sockaddr const *max() const = 0;

  protected:
    void *_data = nullptr; ///< Client data.
  };

  /** Iterator over nodes / intervals.

      The iteration is over all nodes, regardless of which node is
      used to create the iterator. The node passed to the constructor
      just sets the current location.
  */
  class iterator
  {
    friend class IpMap;

  public:
    using self_type         = iterator; ///< Self reference type.
    using value_type        = Node;     ///< Referenced type for iterator.
    using difference_type   = int;      ///< Distance type.
    using pointer           = Node *;   ///< Pointer to referent.
    using reference         = Node &;   ///< Reference to referent.
    using iterator_category = std::bidirectional_iterator_tag;
    /// Default constructor.
    iterator() = default;
    reference operator*() const; //!< value operator
    pointer operator->() const;  //!< dereference operator
    self_type &operator++();     //!< next node (prefix)
    self_type operator++(int);   //!< next node (postfix)
    self_type &operator--();     ///< previous node (prefix)
    self_type operator--(int);   ///< next node (postfix)

    /** Equality.
        @return @c true if the iterators refer to the same node.
    */
    bool operator==(self_type const &that) const;
    /** Inequality.
        @return @c true if the iterators refer to different nodes.
    */
    bool
    operator!=(self_type const &that) const
    {
      return !(*this == that);
    }

  private:
    /// Construct a valid iterator.
    iterator(IpMap const *tree, Node *node) : _tree(tree), _node(node) {}
    IpMap const *_tree = nullptr; ///< Container.
    Node *_node        = nullptr; //!< Current node.
  };

  IpMap()                      = default; ///< Default constructor.
  IpMap(self_type const &that) = delete;
  IpMap(self_type &&that) noexcept;
  ~IpMap(); ///< Destructor.

  self_type &operator=(self_type const &that) = delete;
  self_type &operator                         =(self_type &&that);

  /** Mark a range.
      All addresses in the range [ @a min , @a max ] are marked with @a data.
      @return This object.
  */
  self_type &mark(sockaddr const *min, ///< Minimum value in range.
                  sockaddr const *max, ///< Maximum value in range.
                  void *data = nullptr ///< Client data payload.
  );

  /** Mark a range.
      All addresses in the range [ @a min , @a max ] are marked with @a data.
      @note Convenience overload for IPv4 addresses.
      @return This object.
  */
  self_type &mark(in_addr_t min,       ///< Minimum address (network order).
                  in_addr_t max,       ///< Maximum address (network order).
                  void *data = nullptr ///< Client data.
  );

  /** Mark a range.
      All addresses in the range [ @a min , @a max ] are marked with @a data.
      @note Convenience overload for IPv4 addresses.
      @return This object.
  */
  self_type &mark(IpAddr const &min,   ///< Minimum address (network order).
                  IpAddr const &max,   ///< Maximum address (network order).
                  void *data = nullptr ///< Client data.
  );

  /** Mark an IPv4 address @a addr with @a data.
      This is equivalent to calling @c mark(addr, addr, data).
      @note Convenience overload for IPv4 addresses.
      @return This object.
  */
  self_type &mark(in_addr_t addr,      ///< Address (network order).
                  void *data = nullptr ///< Client data.
  );

  /** Mark a range.
      All addresses in the range [ @a min , @a max ] are marked with @a data.
      @note Convenience overload.
      @return This object.
  */
  self_type &mark(IpEndpoint const *min, ///< Minimum address (network order).
                  IpEndpoint const *max, ///< Maximum address (network order).
                  void *data = nullptr   ///< Client data.
  );

  /** Mark an address @a addr with @a data.
      This is equivalent to calling @c mark(addr, addr, data).
      @note Convenience overload.
      @return This object.
  */
  self_type &mark(IpEndpoint const *addr, ///< Address (network order).
                  void *data = nullptr    ///< Client data.
  );

  /** Unmark addresses.

      All addresses in the range [ @a min , @a max ] are cleared
      (removed from the map), no longer marked.

      @return This object.
  */
  self_type &unmark(sockaddr const *min, ///< Minimum value.
                    sockaddr const *max  ///< Maximum value.
  );
  /// Unmark addresses (overload).
  self_type &unmark(IpAddr const &min, IpAddr const &max);
  /// Unmark addresses (overload).
  self_type &unmark(IpEndpoint const *min, IpEndpoint const *max);
  /// Unmark overload.
  self_type &unmark(in_addr_t min, ///< Minimum of range to unmark.
                    in_addr_t max  ///< Maximum of range to unmark.
  );

  /** Fill addresses.

      This background fills using the range. All addresses in the
      range that are @b not present in the map are added. No
      previously present address is changed.

      @note This is useful for filling in first match tables because @a data for already present
      addresses is not changed.

      @return This object.
  */
  self_type &fill(sockaddr const *min, sockaddr const *max, void *data = nullptr);
  /// Fill addresses (overload).
  self_type &fill(IpEndpoint const *min, IpEndpoint const *max, void *data = nullptr);
  /// Fill addresses (overload).
  self_type &fill(IpAddr const &min, IpAddr const &max, void *data = nullptr);
  /// Fill addresses (overload).
  self_type &fill(in_addr_t min, in_addr_t max, void *data = nullptr);

  /** Test for membership.

      @return @c true if the address is in the map, @c false if not.
      If the address is in the map and @a ptr is not @c nullptr, @c *ptr
      is set to the client data for the address.
  */
  bool contains(sockaddr const *target, ///< Search target (network order).
                void **ptr = nullptr    ///< Client data return.
                ) const;

  /** Test for membership.

      @note Covenience overload for IPv4.

      @return @c true if the address is in the map, @c false if not.
      If the address is in the map and @a ptr is not @c nullptr, @c *ptr
      is set to the client data for the address.
  */
  bool contains(in_addr_t target,    ///< Search target (network order).
                void **ptr = nullptr ///< Client data return.
                ) const;

  /** Test for membership.

      @note Convenience overload for @c IpEndpoint.

      @return @c true if the address is in the map, @c false if not.
      If the address is in the map and @a ptr is not @c nullptr, @c *ptr
      is set to the client data for the address.
  */
  bool contains(IpEndpoint const *target, ///< Search target (network order).
                void **ptr = nullptr      ///< Client data return.
                ) const;

  /** Test for membership.

      @note Convenience overload for @c IpAddr.

      @return @c true if the address is in the map, @c false if not.
      If the address is in the map and @a ptr is not @c nullptr, @c *ptr
      is set to the client data for the address.
  */
  bool contains(IpAddr const &target, ///< Search target (network order).
                void **ptr = nullptr  ///< Client data return.
                ) const;

  /** Remove all addresses from the map.

      @note This is much faster than @c unmark.
      @return This object.
  */
  self_type &clear();

  /// Iterator for first element.
  iterator begin() const;
  /// Iterator past last element.
  iterator end() const;
  /// @return Number of distinct ranges in the map.
  size_t count() const;

  /** Validate internal data structures.
      @note Intended for debugging, not general client use.
  */
  void validate();

  /** Generate formatted output.
   *
   * @param w Destination of formatted output.
   * @param spec Formatting specification.
   * @return @a w
   */
  ts::BufferWriter &describe(ts::BufferWriter &w, ts::BWFSpec const &spec) const;

protected:
  /// Force the IPv4 map to exist.
  /// @return The IPv4 map.
  ts::detail::Ip4Map *force4();
  /// Force the IPv6 map to exist.
  /// @return The IPv6 map.
  ts::detail::Ip6Map *force6();

  ts::detail::Ip4Map *_m4 = nullptr; ///< Map of IPv4 addresses.
  ts::detail::Ip6Map *_m6 = nullptr; ///< Map of IPv6 addresses.
};

inline IpMap &
IpMap::mark(in_addr_t addr, void *data)
{
  return this->mark(addr, addr, data);
}

inline IpMap &
IpMap::mark(IpAddr const &min, IpAddr const &max, void *data)
{
  IpEndpoint x, y;
  x.assign(min);
  y.assign(max);
  return this->mark(&x.sa, &y.sa, data);
}

inline IpMap &
IpMap::mark(IpEndpoint const *addr, void *data)
{
  return this->mark(&addr->sa, &addr->sa, data);
}

inline IpMap &
IpMap::mark(IpEndpoint const *min, IpEndpoint const *max, void *data)
{
  return this->mark(&min->sa, &max->sa, data);
}

inline IpMap &
IpMap::unmark(IpEndpoint const *min, IpEndpoint const *max)
{
  return this->unmark(&min->sa, &max->sa);
}

inline IpMap &
IpMap::unmark(IpAddr const &min, IpAddr const &max)
{
  IpEndpoint x, y;
  x.assign(min);
  y.assign(max);
  return this->unmark(&x.sa, &y.sa);
}

inline IpMap &
IpMap::fill(IpEndpoint const *min, IpEndpoint const *max, void *data)
{
  return this->fill(&min->sa, &max->sa, data);
}

inline IpMap &
IpMap::fill(IpAddr const &min, IpAddr const &max, void *data)
{
  IpEndpoint x, y;
  x.assign(min);
  y.assign(max);
  return this->fill(&x.sa, &y.sa, data);
}

inline bool
IpMap::contains(IpEndpoint const *target, void **ptr) const
{
  return this->contains(&target->sa, ptr);
}

inline bool
IpMap::contains(IpAddr const &addr, void **ptr) const
{
  IpEndpoint ip;
  ip.assign(addr);
  return this->contains(&ip.sa, ptr);
}

inline IpMap::iterator
IpMap::end() const
{
  return iterator(this, nullptr);
}

inline IpMap::iterator
IpMap::iterator::operator++(int)
{
  iterator old(*this);
  ++*this;
  return old;
}

inline IpMap::iterator
IpMap::iterator::operator--(int)
{
  self_type tmp(*this);
  --*this;
  return tmp;
}

inline bool
IpMap::iterator::operator==(iterator const &that) const
{
  return _tree == that._tree && _node == that._node;
}

inline IpMap::iterator::reference IpMap::iterator::operator*() const
{
  return *_node;
}

inline IpMap::iterator::pointer IpMap::iterator::operator->() const
{
  return _node;
}

namespace ts
{
inline BufferWriter &
bwformat(BufferWriter &w, BWFSpec const &spec, IpMap const &map)
{
  return map.describe(w, spec);
}
} // namespace ts
