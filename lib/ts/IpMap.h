# if ! defined(TS_IP_MAP_HEADER)
# define TS_IP_MAP_HEADER

# include "ink_platform.h"
# include "ink_defs.h"
# include <ts/ink_inet.h>
# include <ts/IntrusiveDList.h>
# include <ts/ink_assert.h>

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

namespace ts { namespace detail {

  /** Interval class.
      This holds an interval based on a metric @a T along with
      client data.
  */
  template <
    typename T, ///< Metric for span.
    typename A  = T const& ///< Argument type.
  > struct Interval {
    typedef T Metric; ///< Metric (storage) type.
    typedef A ArgType; ///< Type used to pass instances of @c Metric.

    Interval() {} ///< Default constructor.
    /// Construct with values.
    Interval(
      ArgType min, ///< Minimum value in span.
      ArgType max ///< Maximum value in span.
    ) : _min(min), _max(max) {}
    Metric _min; ///< Minimum value in span.
    Metric _max; ///< Maximum value in span.
  };

  class Ip4Map; // Forward declare.
  class Ip6Map; // Forward declare.

  /** A node in a red/black tree.

      This class provides only the basic tree operations. The client
      must provide the search and decision logic. This enables this
      class to be a base class for templated nodes with much less code
      duplication.
  */
  struct RBNode {
    typedef RBNode self; ///< self reference type

    /// Node colors
    typedef enum { RED, BLACK } Color;

    /// Directional constants
    typedef enum { NONE, LEFT, RIGHT } Direction;

    /// Get a child by direction.
    /// @return The child in the direction @a d if it exists,
    /// @c NULL if not.
    self* getChild(
      Direction d //!< The direction of the desired child
    ) const;

    /** Determine which child a node is
        @return @c LEFT if @a n is the left child,
        @c RIGHT if @a n is the right child,
        @c NONE if @a n is not a child
    */
    Direction getChildDirection(
      self* const& n //!< The presumed child node
    ) const {
      return (n == _left) ? LEFT : (n == _right) ? RIGHT : NONE;
    }

    /** Get the parent node.
        @return A Node* to the parent node or a @c nil Node* if no parent.
    */
    self* getParent() const { return const_cast<self*>(_parent); }

    /// @return The color of the node.
    Color getColor() const { return _color; }

    /** Reverse a direction
        @return @c LEFT if @a d is @c RIGHT, @c RIGHT if @a d is @c LEFT,
        @c NONE otherwise.
    */
    Direction flip(Direction d) {
      return LEFT == d ? RIGHT : RIGHT == d ? LEFT : NONE;
    }

    /** Perform internal validation checks.
        @return 0 on failure, black height of the tree on success.
    */
    int validate();

    /// Default constructor.
    RBNode()
      : _color(RED)
      , _parent(0)
      , _left(0)
      , _right(0)
      , _next(0)
      , _prev(0) {
    }

    /// Destructor (force virtual).
    virtual ~RBNode() { }

    /** Rotate the subtree rooted at this node.
        The node is rotated in to the position of one of its children.
        Which child is determined by the direction parameter @a d. The
        child in the other direction becomes the new root of the subtree.

        If the parent pointer is set, then the child pointer of the original
        parent is updated so that the tree is left in a consistent state.

        @note If there is no child in the other direction, the rotation
        fails and the original node is returned. It is @b not required
        that a child exist in the direction specified by @a d.

        @return The new root node for the subtree.
    */
    self* rotate(
      Direction d //!< The direction to rotate
    );

    /** Set the child node in direction @a d to @a n.
        The @a d child is set to the node @a n. The pointers in this
        node and @a n are set correctly. This can only be called if
        there is no child node already present.

        @return @a n.
    */
    self* setChild(
      self* n, //!< The node to set as the child
      Direction d //!< The direction of the child
    );

    /** Remove this node from the tree.
        The tree is rebalanced after removal.
        @return The new root node.
    */
    self* remove();

    void clearChild(Direction dir) {
      if (LEFT == dir) _left = 0;
      else if (RIGHT == dir) _right = 0;
    }

    /** @name Subclass hook methods */
    //@{
    /** Structural change notification.
        This method is called if the structure of the subtree rooted at
        this node was changed.
			
        This is intended a hook. The base method is empty so that subclasses
        are not required to override.
    */
    virtual void structureFixup() {}

    /** Called from @c validate to perform any additional validation checks.
        Clients should chain this if they wish to perform additional checks.
        @return @c true if the validation is successful, @c false otherwise.
        @note The base method simply returns @c true.
    */
    virtual bool structureValidate() { return true; }
    //@}

    /** Replace this node with another node.
        This is presumed to be non-order modifying so the next reference
        is @b not updated.
    */
    void replaceWith(
      self* n //!< Node to put in place of this node.
    );

    //! Rebalance the tree starting at this node
    /** The tree is rebalanced so that all of the invariants are
        true. The (potentially new) root of the tree is returned.

        @return The root node of the tree after the rebalance.
    */
    self* rebalanceAfterInsert();
		
    /** Rebalance the tree after a deletion.
        Called on the lowest modified node.
        @return The new root of the tree.
    */
    self* rebalanceAfterRemove(
      Color c, //!< The color of the removed node.
      Direction d //!< Direction of removed node from parent
    );

    //! Invoke @c structure_fixup() on this node and all of its ancestors.
    self* rippleStructureFixup();

    Color _color;  ///< node color
    self* _parent; ///< parent node (needed for rotations)
    self* _left;   ///< left child
    self* _right;  ///< right child
    self* _next; ///< Next node.
    self* _prev; ///< Previous node.
  };

}} // namespace ts::detail

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

class IpMap {
public:
  typedef IpMap self; ///< Self reference type.

  class iterator; // forward declare.
  
  /** Public API for intervals in the map.
  */
  class Node : protected ts::detail::RBNode {
    friend class iterator;
    friend class IpMap;
  public:
    typedef Node self; ///< Self reference type.
    /// Default constructor.
    Node() : _data(0) {}
    /// Construct with @a data.
    Node(void* data) : _data(data) {}
    /// @return Client data for the node.
    virtual void* data() { return _data; }
    /// Set client data.
    virtual self& setData(
      void* data ///< Client data pointer to store.
    ) {
      _data = data;
      return *this;
    }
    /// @return Minimum value of the interval.
    virtual sockaddr const* min() const = 0;
    /// @return Maximum value of the interval.
    virtual sockaddr const* max() const = 0;
  protected:
    void* _data; ///< Client data.
  };

  /** Iterator over nodes / intervals.

      The iteration is over all nodes, regardless of which node is
      used to create the iterator. The node passed to the constructor
      just sets the current location.
  */
  class iterator {
    friend class IpMap;
  public:
    typedef iterator self; ///< Self reference type.
    typedef Node value_type; ///< Referenced type for iterator.
    typedef int difference_type; ///< Distance type.
    typedef Node* pointer; ///< Pointer to referent.
    typedef Node& reference; ///< Reference to referent.
    typedef std::bidirectional_iterator_tag iterator_category;
    /// Default constructor.
    iterator() : _tree(0), _node(0) {}

    reference operator* (); //!< value operator
    pointer operator -> (); //!< dereference operator
    self& operator++(); //!< next node (prefix)
    self operator++(int); //!< next node (postfix)
    self& operator--(); ///< previous node (prefix)
    self operator--(int); ///< next node (postfix)

    /** Equality.
        @return @c true if the iterators refer to the same node.
    */
    bool operator==(self const& that) const;
    /** Inequality.
        @return @c true if the iterators refer to different nodes.
    */
    bool operator!=(self const& that) const { return ! (*this == that); }
  private:
    /// Construct a valid iterator.
    iterator(IpMap* tree, Node* node) : _tree(tree), _node(node) {}
      IpMap* _tree; ///< Container.
      Node* _node; //!< Current node.
    };

  IpMap(); ///< Default constructor.
  ~IpMap(); ///< Destructor.

  /** Mark a range.
      All addresses in the range [ @a min , @a max ] are marked with @a data.
      @return This object.
  */
  self& mark(
    sockaddr const* min, ///< Minimum value in range.
    sockaddr const* max, ///< Maximum value in range.
    void* data = 0     ///< Client data payload.
  );

  /** Mark a range.
      All addresses in the range [ @a min , @a max ] are marked with @a data.
      @note Convenience overload for IPv4 addresses.
      @return This object.
  */
  self& mark(
    in_addr_t min, ///< Minimum address (network order).
    in_addr_t max, ///< Maximum address (network order).
    void* data = 0 ///< Client data.
  );

  /** Mark an IPv4 address @a addr with @a data.
      This is equivalent to calling @c mark(addr, addr, data).
      @note Convenience overload for IPv4 addresses.
      @return This object.
  */
  self& mark(
    in_addr_t addr, ///< Address (network order).
    void* data = 0 ///< Client data.
  );

  /** Mark a range.
      All addresses in the range [ @a min , @a max ] are marked with @a data.
      @note Convenience overload.
      @return This object.
  */
  self& mark(
    IpEndpoint const* min, ///< Minimum address (network order).
    IpEndpoint const* max, ///< Maximum address (network order).
    void* data = 0 ///< Client data.
  );

  /** Mark an IPv6 address @a addr with @a data.
      This is equivalent to calling @c mark(addr, addr, data).
      @note Convenience overload.
      @return This object.
  */
  self& mark(
    IpEndpoint const* addr, ///< Address (network order).
    void* data = 0 ///< Client data.
  );

  /** Unmark addresses.

      All addresses in the range [ @a min , @a max ] are cleared
      (removed from the map), no longer marked.

      @return This object.
  */
  self& unmark(
    sockaddr const* min, ///< Minimum value.
    sockaddr const* max  ///< Maximum value.
  );
  /// Unmark addresses (overload).
  self& unmark(
    IpEndpoint const* min,
    IpEndpoint const* max
  );
  /// Unmark overload.
  self& unmark(
    in_addr_t min, ///< Minimum of range to unmark.
    in_addr_t max  ///< Maximum of range to unmark.
  );

  /** Fill addresses.

      This background fills using the range. All addresses in the
      range that are @b not present in the map are added. No
      previously present address is changed.

      @note This is useful for filling in first match tables.

      @return This object.
  */
  self& fill(
    sockaddr const* min,
    sockaddr const* max,
    void* data = 0
  );
  /// Fill addresses (overload).
  self& fill(
    IpEndpoint const* min,
    IpEndpoint const* max,
    void* data = 0
  );
  /// Fill addresses (overload).
  self& fill(
    in_addr_t min,
    in_addr_t max,
    void* data = 0
  );

  /** Test for membership.

      @return @c true if the address is in the map, @c false if not.
      If the address is in the map and @a ptr is not @c NULL, @c *ptr
      is set to the client data for the address.
  */
  bool contains(
    sockaddr const* target, ///< Search target (network order).
    void **ptr = 0 ///< Client data return.
  ) const;

  /** Test for membership.

      @note Covenience overload for IPv4.

      @return @c true if the address is in the map, @c false if not.
      If the address is in the map and @a ptr is not @c NULL, @c *ptr
      is set to the client data for the address.
  */
  bool contains(
    in_addr_t target, ///< Search target (network order).
    void **ptr = 0 ///< Client data return.
  ) const;

  bool contains(
    IpEndpoint const* target, ///< Search target (network order).
    void **ptr = 0 ///< Client data return.
  ) const;

  /** Remove all addresses from the map.

      @note This is much faster than @c unmark.
      @return This object.
  */
  self& clear();

  /// Iterator for first element.
  iterator begin();
  /// Iterator past last element.
  iterator end();
  /// @return Number of distinct ranges in the map.
  size_t getCount() const;

  /** Validate internal data structures.
      @note Intended for debugging, not general client use.
  */
  void validate();

  /// Print all spans.
  /// @return This map.
  //  self& print();

protected:
  /// Force the IPv4 map to exist.
  /// @return The IPv4 map.
  ts::detail::Ip4Map* force4();
  /// Force the IPv6 map to exist.
  /// @return The IPv6 map.
  ts::detail::Ip6Map* force6();
  
  ts::detail::Ip4Map* _m4; ///< Map of IPv4 addresses.
  ts::detail::Ip6Map* _m6; ///< Map of IPv6 addresses.
  
};

inline IpMap& IpMap::mark(in_addr_t addr, void* data) {
  return this->mark(addr, addr, data);
}

inline IpMap& IpMap::mark(IpEndpoint const* addr, void* data) {
  return this->mark(&addr->sa, &addr->sa, data);
}

inline IpMap& IpMap::mark(IpEndpoint const* min, IpEndpoint const* max, void* data) {
  return this->mark(&min->sa, &max->sa, data);
}

inline IpMap& IpMap::unmark(IpEndpoint const* min, IpEndpoint const* max) {
  return this->unmark(&min->sa, &max->sa);
}

inline IpMap& IpMap::fill(IpEndpoint const* min, IpEndpoint const* max, void* data) {
  return this->fill(&min->sa, &max->sa, data);
}

inline bool IpMap::contains(IpEndpoint const* target, void** ptr) const {
  return this->contains(&target->sa, ptr);
}

inline IpMap::iterator
IpMap::end() {
  return iterator(this, 0);
}

inline IpMap::iterator
IpMap::iterator::operator ++ (int) {
  iterator old(*this);
  ++*this;
  return old;
}

inline IpMap::iterator
IpMap::iterator::operator--(int) {
  self tmp(*this);
  --*this;
  return tmp;
}

inline bool
IpMap::iterator::operator == (iterator const& that) const {
  return _tree == that._tree && _node == that._node;
}

inline IpMap::iterator::reference
IpMap::iterator::operator * () {
  return *_node;
}

inline IpMap::iterator::pointer
IpMap::iterator::operator -> () {
  return _node;
}

inline IpMap::IpMap() : _m4(0), _m6(0) {}

# endif // TS_IP_MAP_HEADER
