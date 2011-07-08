# include "IpMap.h"
// # include <iostream>

/*  Don't bother to look at this code if you don't know how a
    red/black tree works. There are so many good references on the
    subject it's a waste to have some inferior version here. The
    methods on @c Node follow the standard implementation except for
    being parameterized by direction (so that, for instance, right
    rotate and left rotate are both done by the @c rotate method with
    a direction argument).
*/

// Validation / printing disabled until I figure out how to generalize so
// as to not tie reporting into a particular project environment.

namespace ts { namespace detail {

/// Equality.
/// @note If @a n is @c NULL it is treated as having the color @c BLACK.
/// @return @c true if @a c and the color of @a n are the same.
inline bool operator == ( RBNode* n, RBNode::Color c ) {
  return c == ( n ? n->getColor() : RBNode::BLACK);
}
/// Equality.
/// @note If @a n is @c NULL it is treated as having the color @c BLACK.
/// @return @c true if @a c and the color of @a n are the same.
inline bool operator == ( RBNode::Color c, RBNode* n ) {
  return n == c;
}

inline RBNode*
RBNode::getChild(Direction d) const {
  return d == RIGHT ? _right
    : d == LEFT ? _left
    : 0
    ;
}

RBNode*
RBNode::rotate(Direction d) {
  self* parent = _parent; // Cache because it can change before we use it.
  Direction child_dir = _parent ? _parent->getChildDirection(this) : NONE;
  Direction other_dir = this->flip(d);
  self* child = this;

  if (d != NONE && this->getChild(other_dir)) {
    child = this->getChild(other_dir);
    this->clearChild(other_dir);
    this->setChild(child->getChild(d), other_dir);
    child->clearChild(d);
    child->setChild(this, d);
    child->structureFixup();
    this->structureFixup();
    if (parent) {
      parent->clearChild(child_dir);
      parent->setChild(child, child_dir);
    } else {
      child->_parent = 0;
    }
  }
  return child;
}

RBNode*
RBNode::setChild(self* n, Direction d) {
  if (n) n->_parent = this;
  if (d == RIGHT) _right = n;
  else if (d == LEFT) _left = n;
  return n;
}

// Returns the root node
RBNode*
RBNode::rippleStructureFixup() {
  self* root = this; // last node seen, root node at the end
  self* p = this;
  while (p) {
    p->structureFixup();
    root = p;
    p = root->_parent;
  }
  return root;
}

void
RBNode::replaceWith(self* n) {
  n->_color = _color;
  if (_parent) {
    Direction d = _parent->getChildDirection(this);
    _parent->setChild(0, d);
    if (_parent != n) _parent->setChild(n, d);
  } else {
    n->_parent = 0;
  }
  n->_left = n->_right = 0;
  if (_left && _left != n) n->setChild(_left, LEFT);
  if (_right && _right != n) n->setChild(_right, RIGHT);
  _left = _right = 0;
}

/* Rebalance the tree. This node is the unbalanced node. */
RBNode*
RBNode::rebalanceAfterInsert() {
  self* x(this); // the node with the imbalance

  while (x && x->_parent == RED) {
    Direction child_dir = NONE;
        
    if (x->_parent->_parent)
      child_dir = x->_parent->_parent->getChildDirection(x->_parent);
    else
      break;
    Direction other_dir(flip(child_dir));
        
    self* y = x->_parent->_parent->getChild(other_dir);
    if (y == RED) {
      x->_parent->_color = BLACK;
      y->_color = BLACK;
      x = x->_parent->_parent;
      x->_color = RED;
    } else {
      if (x->_parent->getChild(other_dir) == x) {
        x = x->_parent;
        x->rotate(child_dir);
      }
      // Note setting the parent color to BLACK causes the loop to exit.
      x->_parent->_color = BLACK;
      x->_parent->_parent->_color = RED;
      x->_parent->_parent->rotate(other_dir);
    }
  }
    
  // every node above this one has a subtree structure change,
  // so notify it. serendipitously, this makes it easy to return
  // the new root node.
  self* root = this->rippleStructureFixup();
  root->_color = BLACK;

  return root;
}


// Returns new root node
RBNode*
RBNode::remove() {
  self* root = 0; // new root node, returned to caller

  /*  Handle two special cases first.
      - This is the only node in the tree, return a new root of NIL
      - This is the root node with only one child, return that child as new root
  */
  if (!_parent && !(_left && _right)) {
    if (_left) {
      _left->_parent = 0;
      root = _left;
      root->_color = BLACK;
    } else if (_right) {
      _right->_parent = 0;
      root = _right;
      root->_color = BLACK;
    } // else that was the only node, so leave @a root @c NULL.
    return root;
  }

  /*  The node to be removed from the tree.
      If @c this (the target node) has both children, we remove
      its successor, which cannot have a left child and
      put that node in place of the target node. Otherwise this
      node has at most one child, so we can remove it.
      Note that the successor of a node with a right child is always
      a right descendant of the node. Therefore, remove_node
      is an element of the tree rooted at this node.
      Because of the initial special case checks, we know
      that remove_node is @b not the root node.
  */
  self* remove_node(_left && _right ? _next : this);

  // This is the color of the node physically removed from the tree.
  // Normally this is the color of @a remove_node
  Color remove_color = remove_node->_color;
  // Need to remember the direction from @a remove_node to @a splice_node
  Direction d(NONE);

  // The child node that will be promoted to replace the removed node.
  // The choice of left or right is irrelevant, as remove_node has at
  // most one child (and splice_node may be NIL if remove_node has no
  // children).
  self* splice_node(remove_node->_left
    ? remove_node->_left
    : remove_node->_right
  );

  if (splice_node) {
    // @c replace_with copies color so in this case the actual color
    // lost is that of the splice_node.
    remove_color = splice_node->_color;
    remove_node->replaceWith(splice_node);
  } else {
    // No children on remove node so we can just clip it off the tree
    // We update splice_node to maintain the invariant that it is
    // the node where the physical removal occurred.
    splice_node = remove_node->_parent;
    // Keep @a d up to date.
    d = splice_node->getChildDirection(remove_node);
    splice_node->setChild(0, d);
  }

  // If the node to pull out of the tree isn't this one, 
  // then replace this node in the tree with that removed
  // node in liu of copying the data over.
  if (remove_node != this) {
    // Don't leave @a splice_node referring to a removed node
    if (splice_node == this) splice_node = remove_node;
    this->replaceWith(remove_node);
  }

  root = splice_node->rebalanceAfterRemove(remove_color, d);
  root->_color = BLACK;
  return root;
}

/**
 * Rebalance tree after a deletion
 * Called on the spliced in node or its parent, whichever is not NIL.
 * This modifies the tree structure only if @a c is @c BLACK.
 */
RBNode*
RBNode::rebalanceAfterRemove(
  Color c, //!< The color of the removed node
  Direction d //!< Direction of removed node from its parent
) {
  self* root;

  if (BLACK == c) { // only rebalance if too much black
    self* n = this;
    self* parent = n->_parent;

    // If @a direction is set, then we need to start at a leaf psuedo-node.
    // This is why we need @a parent, otherwise we could just use @a n.
    if (NONE != d) {
      parent = n;
      n = 0;
    }

    while (parent) { // @a n is not the root
      // If the current node is RED, we can just recolor and be done
      if (n == RED) {
        n->_color = BLACK;
        break;
      } else {
        // Parameterizing the rebalance logic on the directions. We
        // write for the left child case and flip directions for the
        // right child case
        Direction near(LEFT), far(RIGHT);
        if (
          (NONE == d && parent->getChildDirection(n) == RIGHT)
          || RIGHT == d
        ) {
          near = RIGHT;
          far = LEFT;
        }

        self* w = parent->getChild(far); // sibling(n)

        if (w->_color == RED) {
          w->_color = BLACK;
          parent->_color = RED;
          parent->rotate(near);
          w = parent->getChild(far);
        }

        self* wfc = w->getChild(far);
        if (w->getChild(near) == BLACK && wfc == BLACK) {
          w->_color = RED;
          n = parent;
          parent = n->_parent;
          d = NONE; // Cancel any leaf node logic
        } else {
          if (wfc->_color == BLACK) {
            w->getChild(near)->_color = BLACK;
            w->_color = RED;
            w->rotate(far);
            w = parent->getChild(far);
            wfc = w->getChild(far); // w changed, update far child cache.
          }
          w->_color = parent->_color;
          parent->_color = BLACK;
          wfc->_color = BLACK;
          parent->rotate(near);
          break;
        }
      }
    }
  }
  root = this->rippleStructureFixup();
  return root;
}

/** Ensure that the local information associated with each node is
    correct globally This should only be called on debug builds as it
    breaks any efficiencies we have gained from our tree structure.
    */
int
RBNode::validate() {
# if 0
  int black_ht = 0;
  int black_ht1, black_ht2;

  if (_left) {
    black_ht1 = _left->validate();
  }
  else
    black_ht1 = 1;

  if (black_ht1 > 0 && _right)
    black_ht2 = _right->validate();
  else
    black_ht2 = 1;

  if (black_ht1 == black_ht2) {
    black_ht = black_ht1;
    if (this->_color == BLACK)
      ++black_ht;
    else {	// No red-red
      if (_left == RED)
        black_ht = 0;
      else if (_right == RED)
        black_ht = 0;
      if (black_ht == 0)
        std::cout << "Red-red child\n";
    }
  } else {
    std::cout << "Height mismatch " << black_ht1 << " " << black_ht2 << "\n";
  }
  if (black_ht > 0 && !this->structureValidate())
    black_ht = 0;

  return black_ht;
# else
  return 0;
# endif
}

/** Base template class for IP maps.
    This class is templated by the @a N type which must be a subclass
    of @c RBNode. This class carries information about the addresses stored
    in the map. This includes the type, the common argument type, and
    some utility methods to operate on the address.
*/
template <
  typename N ///< Node type.
> struct IpMapBase {
  friend class ::IpMap;
  
  typedef IpMapBase self; ///< Self reference type.
  typedef typename N::ArgType ArgType; ///< Import type.
  typedef typename N::Metric Metric;   ///< Import type.g482

  IpMapBase() : _root(0) {}
  ~IpMapBase() { this->erase(); }

  /** Mark a range.
      All addresses in the range [ @a min , @a max ] are marked with @a data.
      @return This object.
  */
  self& mark(
    ArgType min, ///< Minimum value in range.
    ArgType max, ///< Maximum value in range.
    void* data = 0     ///< Client data payload.
  );
  /** Unmark addresses.

      All addresses in the range [ @a min , @a max ] are cleared
      (removed from the map), no longer marked.

      @return This object.
  */
  self& unmark(
    ArgType min,
    ArgType max
  );

  /** Test for membership.

      @return @c true if the address is in the map, @c false if not.
      If the address is in the map and @a ptr is not @c NULL, @c *ptr
      is set to the client data for the address.
  */
  bool contains(
    ArgType target, ///< Search target value.
    void **ptr = 0 ///< Client data return.
  );

  /** Erase entire map.

      All addresses are discarded.

      @note This is much faster than using @c unmark with a range of
      all addresses.

      @return This map.
  */
  self& erase();

  /** Lower bound for @a target.  @return The node whose minimum value
      is the largest that is not greater than @a target, or @c NULL if
      all minimum values are larger than @a target.
  */
  N* lowerBound(ArgType target);

  /** Insert @a n after @a spot.
      Caller is responsible for ensuring that @a spot is in this container
      and the proper location for @a n.
  */
  void insertAfter(
    N* spot, ///< Node in list.
    N* n ///< Node to insert.
  );
  /** Insert @a n before @a spot.
      Caller is responsible for ensuring that @a spot is in this container
      and the proper location for @a n.
  */
  void insertBefore(
    N* spot, ///< Node in list.
    N* n ///< Node to insert.
  );
  /// Add node @a n as the first node.
  void prepend(
    N* n
  );
  /// Add node @a n as the last node.
  void append(
    N* n
  );
  /// Remove a node.
  void remove(
    N* n ///< Node to remove.
  );

  /** Validate internal data structures.
      @note Intended for debugging, not general client use.
  */
  void validate();

  /// Print all spans.
  /// @return This map.
  self& print();
  
  // Helper methods.
  N* prev(RBNode* n) { return static_cast<N*>(n->_prev); }
  N* next(RBNode* n) { return static_cast<N*>(n->_next); }
  N* parent(RBNode* n) { return static_cast<N*>(n->_parent); }
  N* left(RBNode* n) { return static_cast<N*>(n->_left); }
  N* right(RBNode* n) { return static_cast<N*>(n->_right); }
  N* getHead() { return static_cast<N*>(_list.getHead()); }
  N* getTail() { return static_cast<N*>(_list.getTail()); }

  N* _root; ///< Root node.
  /// In order list of nodes.
  /// For ugly compiler reasons, this is a list of base class pointers
  /// even though we really store @a N instances on it.
  typedef IntrusiveDList<RBNode, &RBNode::_next, &RBNode::_prev> NodeList;
  /// This keeps track of all allocated nodes in order.
  /// Iteration depends on this list being maintained.
  NodeList _list;
};

template < typename N > N*
IpMapBase<N>::lowerBound(ArgType target) {
  N* n = _root; // current node to test.
  N* zret = 0; // best node so far.
  while (n) {
    if (target < n->_min) n = left(n);
    else {
      zret = n; // this is a better candidate.
      if (n->_max < target) n = right(n);
      else break;
    }
  }
  return zret;
}

template < typename N > IpMapBase<N>&
IpMapBase<N>::erase() {
  // Delete everything.
  N* n = static_cast<N*>(_list.getHead());
  while (n) {
    N* x = n;
    n = next(n);
    delete x;
  }
  _list.clear();
  _root = 0;
  return *this;
}

template < typename N > IpMapBase<N>&
IpMapBase<N>::mark(ArgType min, ArgType max, void* payload) {
  N* n = this->lowerBound(min); // current node.
  N* x = 0; // New node, gets set if we re-use an existing one.

  /*  We have lots of special cases here primarily to minimize memory allocation
      by re-using an existing node as often as possible.
  */
  if (n) {
    Metric min_1 = N::deref(min);
    N::dec(min_1);
    if (n->_min == min) {
      // Could be another span further left which is adjacent.
      // Coalesce if the data is the same.
      if (n->_prev && prev(n)->_data == payload && prev(n)->_max == min_1) {
        x = prev(n);
        n = x; // need to back up n because we've moved our frame of reference back.
        x->setMax(max);
      } else if (n->_max <= max) {
        // Span will be subsumed by request span so it's available for use.
        x = n;
        x->setMax(max).setData(payload);
      } else if (n->_data == payload) {
        return *this; // request is covered by existing span with the same data
      } else {
        Metric max_plus = N::deref(max);
        N::inc(max_plus);
        // request span is covered by existing span.
        x = new N(min, max, payload); //
        n->setMin(max_plus); // clip existing.
        this->insertBefore(n, x);
        return *this;
      }
    } else if (n->_data == payload && n->_max >= min_1) {
      x = n;
      // If the existing span covers the requested span, we're done.
      if (x->_max >= max) return *this;
      x->setMax(max);
    } else if (n->_max <= max) {
      // Can only have left skew overlap, otherwise disjoint.
      // Clip if overlap.
      if (n->_max >= min) n->setMax(min_1);
      else if (next(n) && n->_max <= max) {
        // request region covers next span so we can re-use that node.
        x = next(n);
        x->setMin(min).setMax(max).setData(payload);
        n = x; // this gets bumped again, which is correct.
      }
    } else {
      // Existing span covers new span but with a different payload.
      // We split it, put the new span in between and we're done.
      N* r;
      Metric max_plus = N::deref(max);
      N::inc(max_plus);
      x = new N(min, max, payload);
      r = new N(max_plus, n->_max, n->_data);
      n->setMax(min_1);
      this->insertAfter(n, x);
      this->insertAfter(x, r);
      return *this; // done.
    }
    n = next(n); // lower bound span handled, move on.
    if (!x) {
      x = new N(min, max, payload);
      if (n) this->insertBefore(n, x);
      else this->append(x); // note that since n == 0 we'll just return.
    }
  } else {
    x = new N(min, max, payload);
    this->prepend(x);
    n = next(x);
  }

  // At this point, @a x has the node for this span and all existing spans of
  // interest start at or past this span.
  while (n) {
    if (n->_max <= max) {
      x = n;
      n = next(n);
      this->remove(x);
    } else if (n->_min > max) {
      // no overlap so we're done.
      break;
    } else {
      Metric max_plus = N::deref(max);
      // just right overlap, clip and we're done.
      N::inc(max_plus);
      n->setMin(max_plus);
      break;
    }
  }

  return *this;
}

template <typename N> IpMapBase<N>&
IpMapBase<N>::unmark(ArgType min, ArgType max) {
  N* n = this->lowerBound(min);
  N* x; // temp for deletes.

  // Need to handle special case where first span starts to the left.
  if (n && n->_min < min) {
    if (n->_max >= min) { // some overlap
      Metric min_1 = N::deref(min);
      N::dec(min_1);
      if (n->_max > max) {
        // request span is covered by existing span - split existing span.
        Metric max_plus = N::deref(max);
        N::inc(max_plus);
        x = new N(max_plus, n->_max, n->_data);
        n->setMax(min_1);
        this->insertAfter(n, x);
        return *this; // done.
      } else {
        n->setMax(min_1); // just clip overloap.
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
        Metric max_plus = N::deref(max);
        N::inc(max_plus);
        x->setMin(max_plus);
      }
      break;
    }
  }
  return *this;
}

template <typename N> void
IpMapBase<N>::insertAfter(N* spot, N* n) {
  N* c = right(spot);
  if (!c) spot->setChild(n, N::RIGHT);
  else spot->_next->setChild(n, N::LEFT);

  _list.insertAfter(spot, n);
  _root = static_cast<N*>(n->rebalanceAfterInsert());
}

template <typename N> void
IpMapBase<N>::insertBefore(N* spot, N* n) {
  N* c = left(spot);
  if (!c) spot->setChild(n, N::LEFT);
  else spot->_prev->setChild(n, N::RIGHT);

  _list.insertBefore(spot, n);
  _root = static_cast<N*>(n->rebalanceAfterInsert());
}

template <typename N> void
IpMapBase<N>::prepend(N* n) {
  if (!_root) _root = n;
  else _root = static_cast<N*>(_list.getHead()->setChild(n, N::LEFT)->rebalanceAfterInsert());
  _list.prepend(n);
}

template <typename N> void
IpMapBase<N>::append(N* n) {
  if (!_root) _root = n;
  else _root = static_cast<N*>(_list.getTail()->setChild(n, N::RIGHT)->rebalanceAfterInsert());
  _list.append(n);
}

template <typename N> void
IpMapBase<N>::remove(N* n) {
  _root = static_cast<N*>(n->remove());
  _list.take(n);
  delete n;
}

template <typename N> bool
IpMapBase<N>::contains(ArgType x, void** ptr) {
  bool zret = false;
  N* n = _root; // current node to test.
  while (n) {
    if (x < n->_min) n = left(n);
    else if (n->_max < x) n = right(n);
    else {
      if (ptr) *ptr = n->_data;
      zret = true;
      break;
    }
  }
  return zret;
}
//----------------------------------------------------------------------------
template <typename N> void
IpMapBase<N>::validate() {
# if 0
  if (_root) _root->validate();
  for ( Node* n = _list.getHead() ; n ; n = n->_next ) {
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
# endif
}

template <typename N> IpMapBase<N>&
IpMapBase<N>::print() {
# if 0
  for ( Node* n = _list.getHead() ; n ; n = n->_next ) {
    std::cout
      << n << ": " << n->_min << '-' << n->_max << " [" << n->_data << "] "
      << (n->_color == Node::BLACK ? "Black " : "Red   ") << "P=" << n->_parent << " L=" << n->_left << " R=" << n->_right
      << std::endl;
  }
# endif
  return *this;
}

//----------------------------------------------------------------------------
typedef Interval<uint32_t, uint32_t> Ip4Span;

/** Node for IPv4 map.
    We store the address in host order in the @a _min and @a _max
    members for performance. We store copies in the @a _sa member
    for API compliance (which requires @c sockaddr* access).
*/
class Ip4Node : public IpMap::Node, protected Ip4Span {
  friend class IpMapBase<Ip4Node>;
public:
  typedef Ip4Node self; ///< Self reference type.

  /// Construct with values.
  Ip4Node(
    ArgType min, ///< Minimum address (network order).
    ArgType max, ///< Maximum address (network order).
    void* data ///< Client data.
  ) : Node(data), Ip4Span(ntohl(min), ntohl(max)) {
    ink_inet_ip4_set(ink_inet_sa_cast(&_sa._min), min);
    ink_inet_ip4_set(ink_inet_sa_cast(&_sa._max), max);
  }
  /// @return The minimum value of the interval.
  virtual sockaddr const* min() {
    return ink_inet_sa_cast(&_sa._min);
  }
  /// @return The maximum value of the interval.
  virtual sockaddr const* max() {
    return ink_inet_sa_cast(&_sa._max);
  }
  /// Set the client data.
  self& setData(
    void* data ///< Client data.
  ) {
    _data = data;
    return *this;
  }
protected:
  
  /// Set the minimum value of the interval.
  /// @return This interval.
  self& setMin(
    ArgType min ///< Minimum value (host order).
  ) {
    _min = min;
    _sa._min.sin_addr.s_addr = htonl(min);
    return *this;
  }
  
  /// Set the maximum value of the interval.
  /// @return This interval.
  self& setMax(
    ArgType max ///< Maximum value (host order).
  ) {
    _max = max;
    _sa._max.sin_addr.s_addr = htonl(max);
    return *this;
  }
  
  // Static helper methods for Metric.
  
  /** Compare two metrics.
      @return
        - -1 if @a lhs < @a rhs
        -  0 if @a lhs == @a rhs
        -  1 if @a lhs > @a rhs
  */
  static int cmp(
    ArgType lhs,
    ArgType rhs
  ) {
    return lhs < rhs ? -1
      : lhs > rhs ? 1
      : 0
      ;
  }
  
  /// Increment a metric.
  static void inc(
    Metric& m ///< Incremented in place.
  ) {
    ++m;
  }
  
  /// Decrement a metric.
  static void dec(
    Metric& m ///< Decremented in place.
  ) {
    --m;
  }
  
  /// @return Dereferenced @a addr.
  static Metric deref(
    ArgType addr ///< Argument to dereference.
  ) {
    return addr;
  }
  
  struct {
    sockaddr_in _min;
    sockaddr_in _max;
  } _sa; ///< Addresses in API compliant form.

};

class Ip4Map : public IpMapBase<Ip4Node> {
  friend class ::IpMap;
};

//----------------------------------------------------------------------------
typedef Interval<sockaddr_in6> Ip6Span;

/** Node for IPv6 map.
*/
class Ip6Node : public IpMap::Node, protected Ip6Span {
  friend class IpMapBase<Ip6Node>;
public:
  typedef Ip6Node self; ///< Self reference type.
  /// Override @c ArgType from @c Interval because the convention
  /// is to use a pointer, not a reference.
  typedef Metric const* ArgType;

  /// Construct from pointers.
  Ip6Node(
    ArgType min, ///< Minimum address (network order).
    ArgType max, ///< Maximum address (network order).
    void* data ///< Client data.
  ) : Node(data), Ip6Span(*min, *max) {
  }
  /// Construct with values.
  Ip6Node(
    Metric const& min, ///< Minimum address (network order).
    Metric const& max, ///< Maximum address (network order).
    void* data ///< Client data.
  ) : Node(data), Ip6Span(min, max) {
  }
  /// @return The minimum value of the interval.
  virtual sockaddr const* min() {
    return ink_inet_sa_cast(&_min);
  }
  /// @return The maximum value of the interval.
  virtual sockaddr const* max() {
    return ink_inet_sa_cast(&_max);
  }
  /// Set the client data.
  self& setData(
    void* data ///< Client data.
  ) {
    _data = data;
    return *this;
  }
protected:
  
  /// Set the minimum value of the interval.
  /// @return This interval.
  self& setMin(
    ArgType min ///< Minimum value (host order).
  ) {
    ink_inet_copy(ink_inet_sa_cast(&_min), ink_inet_sa_cast(min));
    return *this;
  }
  
  /// Set the minimum value of the interval.
  /// @note Convenience overload.
  /// @return This interval.
  self& setMin(
    Metric const& min ///< Minimum value (host order).
  ) {
    return this->setMin(&min);
  }
  
  /// Set the maximum value of the interval.
  /// @return This interval.
  self& setMax(
    ArgType max ///< Maximum value (host order).
  ) {
    ink_inet_copy(ink_inet_sa_cast(&_max), ink_inet_sa_cast(max));
    return *this;
  }
  /// Set the maximum value of the interval.
  /// @note Convenience overload.
  /// @return This interval.
  self& setMax(
    Metric const& max ///< Maximum value (host order).
  ) {
    return this->setMax(&max);
  }
  
  // Static helper methods for Metric.
  
  /** Compare two metrics.
      @return
        - -1 if @a lhs < @a rhs
        -  0 if @a lhs == @a rhs
        -  1 if @a lhs > @a rhs
  */
  static int cmp(
    ArgType lhs,
    ArgType rhs
  ) {
    return ink_inet_cmp(ink_inet_sa_cast(lhs), ink_inet_sa_cast(rhs));
  }
  
  /// Increment a metric.
  static void inc(
    Metric& m ///< Incremented in place.
  ) {
    uint8_t* addr = m.sin6_addr.s6_addr;
    uint8_t* b = addr + INK_IP6_SIZE;
    do {
      ++*--b;
    } while (b > addr && 0 == *b);
  }
  
  /// Decrement a metric.
  static void dec(
    Metric& m ///< Decremented in place.
  ) {
    uint8_t* addr = m.sin6_addr.s6_addr;
    uint8_t* b = addr + INK_IP6_SIZE;
    do {
      --*--b;
    } while (b > addr && static_cast<uint8_t>(0xFF) == *b);
  }
  /// @return Dereferenced @a addr.
  static Metric const& deref(
    ArgType addr ///< Argument to dereference.
  ) {
    return *addr;
  }
  
};

inline int cmp(sockaddr_in6 const& lhs, sockaddr_in6 const& rhs) {
  return memcmp(lhs.sin6_addr.__in6_u.__u6_addr8, rhs.sin6_addr.__in6_u.__u6_addr8, INK_IP6_SIZE);
}

// Helper functions

/// Less than.
inline bool operator<(sockaddr_in6 const* lhs, sockaddr_in6 const& rhs) {
  return -1 == ts::detail::cmp(*lhs, rhs);
}
/// Less than.
inline bool operator<(sockaddr_in6 const& lhs, sockaddr_in6 const* rhs) {
  return -1 == ts::detail::cmp(lhs, *rhs);
}
/// Equality.
inline bool operator==(sockaddr_in6 const& lhs, sockaddr_in6 const* rhs) {
  return 0 == ts::detail::cmp(lhs, *rhs);
}
/// Equality.
inline bool operator==(sockaddr_in6 const* lhs, sockaddr_in6 const& rhs) {
  return 0 == ts::detail::cmp(*lhs, rhs);
}
/// Equality.
inline bool operator==(sockaddr_in6 const& lhs, sockaddr_in6 const& rhs) {
  return 0 == ts::detail::cmp(lhs, rhs);
}
/// Less than or equal.
inline bool operator<=(sockaddr_in6 const& lhs, sockaddr_in6 const* rhs) {
  return 1 != ts::detail::cmp(lhs, *rhs);
}
/// Greater than or equal.
inline bool operator>=(sockaddr_in6 const& lhs, sockaddr_in6 const& rhs) {
  return -1 != ts::detail::cmp(lhs, rhs);
}
/// Greater than or equal.
inline bool operator>=(sockaddr_in6 const& lhs, sockaddr_in6 const* rhs) {
  return -1 != ts::detail::cmp(lhs, *rhs);
}
/// Greater than.
inline bool operator>(sockaddr_in6 const& lhs, sockaddr_in6 const* rhs) {
  return 1 == ts::detail::cmp(lhs, *rhs);
}

class Ip6Map : public IpMapBase<Ip6Node> {
  friend class ::IpMap;
};

}} // end namespaces

//----------------------------------------------------------------------------
namespace {
  ///< @return The network order IPv4 address in @a target.
  inline uint32_t const& ip4_addr(sockaddr const* target) {
    return ink_inet_ip4_cast(target)->sin_addr.s_addr;
  }
}
IpMap::~IpMap() {
  delete _m4;
  delete _m6;
}

inline ts::detail::Ip4Map*
IpMap::force4() {
  if (!_m4) _m4 = new ts::detail::Ip4Map;
  return _m4;
}

bool
IpMap::contains(sockaddr const* target, void** ptr) {
  bool zret = false;
  if (AF_INET == target->sa_family) {
    if (_m4) {
      zret = _m4->contains(ntohl(ip4_addr(target)));
    }
  } else if (AF_INET6 == target->sa_family) {
    if (_m6) {
      zret = _m6->contains(ink_inet_ip6_cast(target));
    }
  }
  return zret;
}

IpMap&
IpMap::mark(
  sockaddr const* min,
  sockaddr const* max,
  void* data
) {
  ink_assert(min->sa_family == max->sa_family);
  if (AF_INET == min->sa_family) {
    this->force4()->mark(ip4_addr(min), ip4_addr(max), data);
  } else if (AF_INET6 == min->sa_family) {
    if (!_m6) _m6 = new ts::detail::Ip6Map;
    _m6->mark(ink_inet_ip6_cast(min), ink_inet_ip6_cast(max));
  }
  return *this;
}

IpMap&
IpMap::mark(uint32_t min, uint32_t max, void* data) {
  this->force4()->mark(min, max, data);
  return *this;
}

IpMap&
IpMap::unmark(
  sockaddr const* min,
  sockaddr const* max
) {
  ink_assert(min->sa_family == max->sa_family);
  if (AF_INET == min->sa_family) {
    if (_m4) _m4->unmark(ip4_addr(min), ip4_addr(max));
  } else if (AF_INET6 == min->sa_family) {
    if (_m6) _m6->unmark(ink_inet_ip6_cast(min), ink_inet_ip6_cast(max));
  }
  return *this;
}

IpMap::iterator
IpMap::begin() {
  Node* x = 0;
  if (_m4) x = _m4->getHead();
  if (!x && _m6) x = _m6->getHead();
  return iterator(this, x);
}

IpMap::iterator&
IpMap::iterator::operator ++ () {
  if (_node) {
    // If we go past the end of the list see if it was the v4 list
    // and if so, move to the v6 list (if it's there).
    Node* x = static_cast<Node*>(_node->_next);
    if (!x && _tree->_m4 && _tree->_m6 && _node == _tree->_m4->getTail())
      x = _tree->_m6->getTail();
    _node = x;
  }
  return *this;
}

inline IpMap::iterator&
IpMap::iterator::operator--() {
  if (_node) {
    // At a node, try to back up. Handle the case where we back over the
    // start of the v6 addresses and switch to the v4, if there are any.
    Node* x = static_cast<Node*>(_node->_prev);
    if (!x && _tree->_m4 && _tree->_m6 && _node == _tree->_m6->getHead())
      x = _tree->_m4->getTail();
    _node = x;
  } else if (_tree) {
    // We were at the end. Back up to v6 if possible, v4 if not.
    if (_tree->_m6) _node = _tree->_m6->getTail();
    if (!_node && _tree->_m4) _node = _tree->_m4->getTail();
  }
  return *this;
}


//----------------------------------------------------------------------------
//----------------------------------------------------------------------------

