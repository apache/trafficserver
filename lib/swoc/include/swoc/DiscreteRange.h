// SPDX-License-Identifier: Apache-2.0
// Copyright 2014 Network Geographics
/** @file
    Support classes for creating intervals of numeric values.

    The template class can be used directly via a @c typedef or
    used as a base class if additional functionality is required.
 */

#pragma once
#include <limits>
#include <functional>

#include "swoc/swoc_version.h"
#include "swoc/swoc_meta.h"
#include "swoc/RBTree.h"
#include "swoc/MemArena.h"

namespace swoc { inline namespace SWOC_VERSION_NS {
namespace detail {

/// A set of metafunctions to get extrema from a metric type.
/// These probe for a static member and falls back to @c std::numeric_limits.
/// @{

/** Maximum value.
 *
 * @tparam M Metric type.
 * @return Maximum value for @a M.
 *
 * Use @c std::numeric_limits.
 */
template <typename M>
constexpr auto
maximum(meta::CaseTag<0>) -> M {
  return std::numeric_limits<M>::max();
}

/** Maximum value.
 *
 * @tparam M Metric type.
 * @return Maximum value for @a M.
 *
 * Use @c M::MAX
 */
template <typename M>
constexpr auto
maximum(meta::CaseTag<1>) -> decltype(M::MAX) {
  return M::MAX;
}

/** Maximum value.
 *
 * @tparam M Metric type.
 * @return Maximum value for @a M.
 */
template <typename M>
constexpr M
maximum() {
  return maximum<M>(meta::CaseArg);
}

/** Minimum value.
 *
 * @tparam M Metric type.
 * @return Minimum value for @a M
 *
 * Use @c std::numeric_limits
 */
template <typename M>
constexpr auto
minimum(meta::CaseTag<0>) -> M {
  return std::numeric_limits<M>::min();
}

/** Minimum value.
 *
 * @tparam M Metric type.
 * @return Minimum value for @a M
 *
 * Use @c M::MIN
 */
template <typename M>
constexpr auto
minimum(meta::CaseTag<1>) -> decltype(M::MIN) {
  return M::MIN;
}

/** Minimum value.
 *
 * @tparam M Metric type.
 * @return Minimum value for @a M
 */
template <typename M>
constexpr M
minimum() {
  return minimum<M>(meta::CaseArg);
}
/// @}
} // namespace detail

/// Relationship between two intervals.
enum class DiscreteRangeRelation : uint8_t {
  NONE,     ///< No common elements.
  EQUAL,    ///< Identical ranges.
  SUBSET,   ///< All elements in LHS are also in RHS.
  SUPERSET, ///< Every element in RHS is in LHS.
  OVERLAP,  ///< There exists at least one element in both LHS and RHS.
  ADJACENT  ///< The two intervals are adjacent and disjoint.
};

/// Relationship between one edge of an interval and the "opposite" edge of another.
enum class DiscreteRangeEdgeRelation : uint8_t {
  NONE, ///< Edge is on the opposite side of the relating edge.
  GAP,  ///< There is a gap between the edges.
  ADJ,  ///< The edges are adjacent.
  OVLP, ///< Edge is inside interval.
};

/** A range over a discrete finite value metric.
   @tparam T The type for the range values.

   The template argument @a T is presumed to
   - be completely ordered.
   - have prefix increment and decrement operators
   - equality operator
   - have value semantics
   - have minimum and maximum values either
     - members @c MIN and @c MAX that define static instances
     - @c std::numeric_limits<T> support.

   The interval is always an inclusive (closed) contiguous interval,
   defined by the minimum and maximum values contained in the interval.
   An interval can be @em empty and contain no values. This is the state
   of a default constructed interval.
 */
template <typename T> class DiscreteRange {
  using self_type = DiscreteRange;

protected:
  T _min; ///< The minimum value in the interval
  T _max; ///< the maximum value in the interval

public:
  using metric_type  = T;                         ///< Export metric type.
  using Relation     = DiscreteRangeRelation;     ///< Import type for convenience.
  using EdgeRelation = DiscreteRangeEdgeRelation; ///< Import type for convenience.

  /** Default constructor.
   * An invalid (empty) range is constructed.
   */
  constexpr DiscreteRange() : _min(detail::maximum<T>()), _max(detail::minimum<T>()) {}

  /** Construct a singleton range.
   * @param value Single value to be contained by the interval.
   *
   * @note Not marked @c explicit and so serves as a conversion from scalar values to an interval.
   */
  constexpr DiscreteRange(T const &value) : _min(value), _max(value){};

  /** Constructor.
   *
   * @param min Minimum value in the interval.
   * @param max Maximum value in the interval.
   */
  constexpr DiscreteRange(T const &min, T const &max) : _min(min), _max(max) {}

  ~DiscreteRange() = default;

  /** Check if there are no values in the range.
   *
   * @return @c true if the range is empty (contains no values), @c false if it contains at least
   * one value.
   */
  bool empty() const;

  /** Update the range.
   *
   * @param min New minimum value.
   * @param max New maximum value.
   * @return @a this.
   */
  self_type &assign(metric_type const &min, metric_type const &max);

  /** Update the range.
   *
   * @param value The new minimum and maximum value.
   * @return @a this.
   *
   * The range will contain the single value @a value.
   */
  self_type &assign(metric_type const &value);

  /** Update the minimum value.
   *
   * @param min The new minimum value.
   * @return @a this.
   *
   * @note No checks are done - this can result in an empty range.
   */
  self_type &assign_min(metric_type const &min);

  /** Update the maximum value.
   *
   * @param max The new maximum value.
   * @return @a this.
   *
   * @note No checks are done - this can result in an empty range.
   */
  self_type &assign_max(metric_type const &max);

  /** Decrement the maximum value.
   *
   * @return @a this.
   *
   * @note No checks are done, the caller must ensure it is valid to decremented the current maximum.
   */
  self_type &clip_max();

  /** Minimum value.
   * @return the minimum value in the range.
   * @note The return value is unspecified if the interval is empty.
   */
  metric_type const &min() const;

  /** Maximum value.
   * @return The maximum value in the range.
   * @note The return value is unspecified if the interval is empty.
   */
  metric_type const &max() const;

  /// Equality.
  bool operator==(self_type const &that) const;

  /// Inequality.
  bool operator!=(self_type const &that) const;

  /** Check if a value is in @a this range.
   *
   * @param value Metric value to check.
   * @return @c true if @a value is in the range, @c false if not.
   */
  bool contains(metric_type const &value) const;

  /** Logical intersection.
   * @return @c true if there is at least one common value in the two intervals, @c false otherwise.
   */
  bool has_intersection_with(self_type const &that) const;

  /** Compute the intersection of two intervals
   *
   * @return The interval consisting of values that are contained by both intervals. The return range
   * is empty if the intervals are disjoint.
   *
   * @internal Co-variant
   */
  self_type intersection(self_type const &that) const;

  /** Test for adjacency.
   * @return @c true if the intervals are adjacent.
   * @note Only disjoint intervals can be adjacent.
   */
  bool is_adjacent_to(self_type const &that) const;

  /** Lower adjacency.
   *
   * @param that Range to check for adjacency.
   * @return @c true if @a this and @ta that are adjacent and @a this is less than @a that.
   */
  bool is_left_adjacent_to(self_type const &that) const;

  /** Valid union.
   *
   * @param that Range to compare.
   *
   * @return @a true if the hull of @a this and @a that contains only elements that are also in
   * @a this or @a that.
   */
  bool has_union(self_type const &that) const;

  /** Superset test.
   *
   * @return @c true if every value in @c that is also in @c this.
   */
  bool is_superset_of(self_type const &that) const;

  /** Subset test.
   *
   *  @return @c true if every value in @c this is also in @c that.
   */
  bool is_subset_of(self_type const &that) const;

  /** Strict superset test.
   *
   * @return @c true if @a this contains every value in @a this and @a this has at least one value not
   * in @a that.
   */
  bool is_strict_superset_of(self_type const &that) const;

  /** Strict subset test.
   *
   * @return @c true if @a that contains every value in @a this and @a that has at least one value not
   * in @a this.
   */
  bool is_strict_subset_of(self_type const &that) const;

  /** Generic relationship.
   *
   * @return The relationship between @a this and @a that.
   */
  Relation relationship(self_type const &that) const;

  /** Determine the relationship of the left edge of @a that with @a this.
   *
   * @param that The other interval.
   * @return The edge relationship.
   *
   * This checks the right edge of @a this against the left edge of @a that.
   *
   * - GAP: @a that left edge is right of @a this.
   * - ADJ: @a that left edge is right adjacent to @a this.
   * - OVLP: @a that left edge is inside @a this.
   * - NONE: @a that left edge is left of @a this.
   */
  EdgeRelation left_edge_relationship(self_type const &that) const;

  /** Convex hull.
   * @return The smallest interval that is a superset of @c this and @a that.
   * @internal Co-variant
   */
  self_type hull(self_type const &that) const;

  //! Check if the interval is exactly one element.
  bool is_singleton() const;

  /** Test for empty, operator form.
      @return @c true if the interval is empty, @c false otherwise.
   */
  bool operator!() const;

  /** Test for non-empty.
   *
   * @return @c true if there values in the range, @c false if no values in the range.
   */
  explicit operator bool() const;

  /** Maximality.
   * @return @c true if this range contains every value.
   */
  bool is_maximal() const;

  /** Clip interval.
   *  Remove all element in @c this interval not in @a that interval.
   */
  self_type &operator&=(self_type const &that);

  /** Convex hull.
   * Minimally extend @a this to cover all elements in @c this and @a that.
   * @return @a this.
   */
  self_type &operator|=(self_type const &that);

  /// Make the range empty.
  self_type &clear();

  /** Functor for lexicographic ordering.
      If, for some reason, an interval needs to be put in a container
      that requires a strict weak ordering, the default @c operator @c < will
      not work. Instead, this functor should be used as the comparison
      functor. E.g.
      @code
      typedef std::set<Interval<T>, Interval<T>::lexicographic_order> container;
      @endcode

      @note Lexicographic ordering is a standard tuple ordering where the
      order is determined by pairwise comparing the elements of both tuples.
      The first pair of elements that are not equal determine the ordering
      of the overall tuples.
   */
  struct lexicographic_order {
    using first_argument_type  = self_type;
    using second_argument_type = self_type;
    using result_type          = bool;

    //! Functor operator.
    bool operator()(self_type const &lhs, self_type const &rhs) const;
  };
};

template <typename T>
bool
DiscreteRange<T>::operator==(DiscreteRange::self_type const &that) const {
  return _min == that._min && _max == that._max;
}

template <typename T>
bool
DiscreteRange<T>::operator!=(DiscreteRange::self_type const &that) const {
  return _min != that._min | _max != that._max;
}

template <typename T>
auto
DiscreteRange<T>::clip_max() -> self_type & {
  --_max;
  return *this;
}

template <typename T>
bool
DiscreteRange<T>::operator!() const {
  return _min > _max;
}

template <typename T> DiscreteRange<T>::operator bool() const {
  return _min <= _max;
}

template <typename T>
bool
DiscreteRange<T>::contains(metric_type const &value) const {
  return _min <= value && value <= _max;
}

template <typename T>
auto
DiscreteRange<T>::clear() -> self_type & {
  _min = detail::maximum<T>();
  _max = detail::minimum<T>();
  return *this;
}

template <typename T>
bool
DiscreteRange<T>::lexicographic_order::operator()(DiscreteRange::self_type const &lhs, DiscreteRange::self_type const &rhs) const {
  return lhs._min == rhs._min ? lhs._max < rhs._max : lhs._min < rhs._min;
}

template <typename T>
DiscreteRange<T> &
DiscreteRange<T>::assign(metric_type const &min, metric_type const &max) {
  _min = min;
  _max = max;
  return *this;
}

template <typename T>
DiscreteRange<T>
DiscreteRange<T>::hull(DiscreteRange::self_type const &that) const {
  // need to account for invalid ranges.
  return !*this ? that : !that ? *this : self_type(std::min(_min, that._min), std::max(_max, that._max));
}

template <typename T>
auto
DiscreteRange<T>::left_edge_relationship(self_type const &that) const -> EdgeRelation {
  if (_max < that._max) {
    return ++metric_type(_max) < that._max ? EdgeRelation::GAP : EdgeRelation::ADJ;
  }
  return _min >= that._min ? EdgeRelation::NONE : EdgeRelation::OVLP;
}

template <typename T>
auto
DiscreteRange<T>::relationship(self_type const &that) const -> Relation {
  Relation retval = Relation::NONE;
  if (this->has_intersection_with(that)) {
    if (*this == that)
      retval = Relation::EQUAL;
    else if (this->is_subset_of(that))
      retval = Relation::SUBSET;
    else if (this->is_superset_of(that))
      retval = Relation::SUPERSET;
    else
      retval = Relation::OVERLAP;
  } else if (this->is_adjacent_to(that)) {
    retval = Relation::ADJACENT;
  }
  return retval;
}

template <typename T>
DiscreteRange<T> &
DiscreteRange<T>::assign(metric_type const &singleton) {
  _min = singleton;
  _max = singleton;
  return *this;
}

template <typename T>
DiscreteRange<T> &
DiscreteRange<T>::assign_min(metric_type const &min) {
  _min = min;
  return *this;
}

template <typename T>
bool
DiscreteRange<T>::is_singleton() const {
  return _min == _max;
}

template <typename T>
bool
DiscreteRange<T>::empty() const {
  return _min > _max;
}

template <typename T>
bool
DiscreteRange<T>::is_maximal() const {
  return _min == detail::minimum<T>() && _max == detail::maximum<T>();
}

template <typename T>
bool
DiscreteRange<T>::is_strict_superset_of(DiscreteRange::self_type const &that) const {
  return (_min < that._min && that._max <= _max) || (_min <= that._min && that._max < _max);
}

template <typename T>
DiscreteRange<T> &
DiscreteRange<T>::operator|=(DiscreteRange::self_type const &that) {
  if (!*this) {
    *this = that;
  } else if (that) {
    if (that._min < _min) {
      _min = that._min;
    }
    if (that._max > _max) {
      _max = that._max;
    }
  }
  return *this;
}

template <typename T>
DiscreteRange<T> &
DiscreteRange<T>::assign_max(metric_type const &max) {
  _max = max;
  return *this;
}

template <typename T>
bool
DiscreteRange<T>::is_strict_subset_of(DiscreteRange::self_type const &that) const {
  return that.is_strict_superset_of(*this);
}

template <typename T>
bool
DiscreteRange<T>::is_subset_of(DiscreteRange::self_type const &that) const {
  return that.is_superset_of(*this);
}

template <typename T>
T const &
DiscreteRange<T>::min() const {
  return _min;
}

template <typename T>
T const &
DiscreteRange<T>::max() const {
  return _max;
}

template <typename T>
bool
DiscreteRange<T>::has_union(DiscreteRange::self_type const &that) const {
  return this->has_intersection_with(that) || this->is_adjacent_to(that);
}

template <typename T>
DiscreteRange<T> &
DiscreteRange<T>::operator&=(DiscreteRange::self_type const &that) {
  *this = this->intersection(that);
  return *this;
}

template <typename T>
bool
DiscreteRange<T>::has_intersection_with(DiscreteRange::self_type const &that) const {
  return (that._min <= _min && _min <= that._max) || (_min <= that._min && that._min <= _max);
}

template <typename T>
bool
DiscreteRange<T>::is_superset_of(DiscreteRange::self_type const &that) const {
  return _min <= that._min && that._max <= _max;
}

template <typename T>
bool
DiscreteRange<T>::is_adjacent_to(DiscreteRange::self_type const &that) const {
  return this->is_left_adjacent_to(that) || that.is_left_adjacent_to(*this);
}

template <typename T>
bool
DiscreteRange<T>::is_left_adjacent_to(DiscreteRange::self_type const &that) const {
  /* Need to be careful here. We don't know much about T and we certainly don't know if "t+1"
   * even compiles for T. We do require the increment operator, however, so we can use that on a
   * copy to get the equivalent of t+1 for adjacency testing. We must also handle the possibility
   * T has a modulus and not depend on ++t > t always being true. However, we know that if t1 >
   * t0 then ++t0 > t0.
   */
  metric_type max(_max); // some built in integer types don't support increment on rvalues.
  return _max < that._min && ++max == that._min;
}

template <typename T>
DiscreteRange<T>
DiscreteRange<T>::intersection(DiscreteRange::self_type const &that) const {
  return {std::max(_min, that._min), std::min(_max, that._max)};
}

template <typename T>
bool
operator==(DiscreteRange<T> const &lhs, DiscreteRange<T> const &rhs) {
  return lhs.min() == rhs.min() && lhs.max() == rhs.max();
}

/** Inequality.
    Two intervals are equal if their min and max values are equal.
    @relates DiscreteRange
 */
template <typename T>
bool
operator!=(DiscreteRange<T> const &lhs, DiscreteRange<T> const &rhs) {
  return !(lhs == rhs);
}

/** Operator form of logical intersection test for two intervals.
    @return @c true if there is at least one common value in the
    two intervals, @c false otherwise.
    @note Yeah, a bit ugly, using an operator that is not standardly
    boolean but
    - There don't seem to be better choices (&&,|| not good)
    - The assymmetry between intersection and union makes for only three natural operators
    - ^ at least looks like "intersects"
    @relates DiscreteRange
 */
template <typename T>
bool
operator^(DiscreteRange<T> const &lhs, DiscreteRange<T> const &rhs) {
  return lhs.has_intersection_with(rhs);
}

/** Containment ordering.
    @return @c true if @c this is a strict subset of @a rhs.
    @note Equivalent to @c is_strict_subset.
    @relates DiscreteRange
 */
template <typename T>
inline bool
operator<(DiscreteRange<T> const &lhs, DiscreteRange<T> const &rhs) {
  return rhs.is_strict_superset_of(lhs);
}

/** Containment ordering.
    @return @c true if @c this is a subset of @a rhs.
    @note Equivalent to @c is_subset.
    @relates DiscreteRange
 */
template <typename T>
inline bool
operator<=(DiscreteRange<T> const &lhs, DiscreteRange<T> const &rhs) {
  return rhs.is_superset_of(lhs);
}

/** Containment ordering.
    @return @c true if @c this is a strict superset of @a rhs.
    @note Equivalent to @c is_strict_superset.
    @relates DiscreteRange
 */
template <typename T>
inline bool
operator>(DiscreteRange<T> const &lhs, DiscreteRange<T> const &rhs) {
  return lhs.is_strict_superset_of(rhs);
}

/** Containment ordering.
    @return @c true if @c this is a superset of @a rhs.
    @note Equivalent to @c is_superset.
    @relates DiscreteRange
    */
template <typename T>
inline bool
operator>=(DiscreteRange<T> const &lhs, DiscreteRange<T> const &rhs) {
  return lhs.is_superset_of(rhs);
}

/** A space for a discrete @c METRIC.
 *
 * @tparam METRIC Value type for the space.
 * @tparam PAYLOAD Data stored with values in the space.
 *
 * This is a range based mapping of all values in @c METRIC (the "space") to @c PAYLOAD.
 *
 * @c PAYLOAD is presumed to be relatively cheap to construct and copy.
 *
 * @c METRIC must be
 * - discrete and finite valued type with increment and decrement operations.
 */
template <typename METRIC, typename PAYLOAD> class DiscreteSpace {
  using self_type = DiscreteSpace;

protected:
  using metric_type  = METRIC;  ///< Export.
  using payload_type = PAYLOAD; ///< Export.
  using range_type   = DiscreteRange<METRIC>;

  /// A node in the range tree.
  class Node : public detail::RBNode {
    using self_type  = Node;           ///< Self reference type.
    using super_type = detail::RBNode; ///< Parent class.
    friend class DiscreteSpace;

    range_type _range;  ///< Range covered by this node.
    range_type _hull;   ///< Range covered by subtree rooted at this node.
    PAYLOAD _payload{}; ///< Default constructor, should zero init if @c PAYLOAD is a pointer.

  public:
    /// Linkage for @c IntrusiveDList.
    using Linkage = swoc::IntrusiveLinkageRebind<self_type, super_type::Linkage>;

    Node() = default; ///< Construct empty node.

    /// Construct from @a range and @a payload.
    Node(range_type const &range, PAYLOAD const &payload) : _range(range), _payload(payload) {}

    /// Construct from two metrics and a payload
    Node(METRIC const &min, METRIC const &max, PAYLOAD const &payload) : _range(min, max), _payload(payload) {}

    /// @return The payload in the node.
    PAYLOAD &payload();

    /** Set the @a range of a node.
     *
     * @param range Range to use.
     * @return @a this
     */
    self_type &assign(range_type const &range);

    /** Set the @a payload for @a this node.
     *
     * @param payload Payload to use.
     * @return @a this
     */
    self_type &assign(PAYLOAD const &payload);

    range_type const &
    range() const {
      return _range;
    }

    self_type &
    assign_min(METRIC const &m) {
      _range.assign_min(m);
      this->ripple_structure_fixup();
      return *this;
    }

    self_type &
    assign_max(METRIC const &m) {
      _range.assign_max(m);
      this->ripple_structure_fixup();
      return *this;
    }

    METRIC const &
    min() const {
      return _range.min();
    }

    METRIC const &
    max() const {
      return _range.max();
    }

    void structure_fixup() override;

    self_type *
    left() {
      return static_cast<self_type *>(_left);
    }

    self_type *
    right() {
      return static_cast<self_type *>(_right);
    }
  };

  using Direction = typename Node::Direction;

  Node *_root = nullptr;                        ///< Root node.
  IntrusiveDList<typename Node::Linkage> _list; ///< In order list of nodes.
  swoc::MemArena _arena{4000};                  ///< Memory Storage.
  swoc::FixedArena<Node> _fa{_arena};           ///< Node allocator and free list.

  // Utility methods to avoid having casts scattered all over.
  Node *
  prev(Node *n) {
    return Node::Linkage::prev_ptr(n);
  }

  Node *
  next(Node *n) {
    return Node::Linkage::next_ptr(n);
  }

  Node *
  left(Node *n) {
    return static_cast<Node *>(n->_left);
  }

  Node *
  right(Node *n) {
    return static_cast<Node *>(n->_right);
  }

public:
  using iterator       = typename decltype(_list)::iterator;
  using const_iterator = typename decltype(_list)::const_iterator;

  DiscreteSpace() = default;

  ~DiscreteSpace();

  /** Set the @a payload for a @a range
   *
   * @param range Range to mark.
   * @param payload Payload to set.
   * @return @a this
   *
   * Values in @a range are set to @a payload regardless of the current state.
   */
  self_type &mark(range_type const &range, PAYLOAD const &payload);

  /** Erase a @a range.
   *
   * @param range Range to erase.
   * @return @a this
   *
   * All values in @a range are removed from the space.
   */
  self_type &erase(range_type const &range);

  /** Blend a @a color to a @a range.
   *
   * @tparam F Functor to blend payloads.
   * @tparam U type to blend in to payloads.
   * @param range Range for blending.
   * @param color Payload to blend.
   * @param blender Functor to compute blended color.
   * @return @a this
   *
   * @a color is blended to values in @a range. If an address in @a range does not have a payload,
   * its payload is set a default constructed @c PAYLOAD blended with @a color. If such an address
   * does have a payload, @a color is blended in to that payload using @a blender. The existing color
   * is passed as the first argument and @a color as the second argument. The functor is expected to
   * update the first argument to be the blended color. The function must return a @c bool to
   * indicate whether the blend resulted in a valid color. If @c false is returned, the blended
   * region is removed from the space.
   */
  template <typename F, typename U = PAYLOAD> self_type &blend(range_type const &range, U const &color, F &&blender);

  /** Fill @a range with @a payload.
   *
   * @param range Range to fill.
   * @param payload Payload to use.
   * @return @a this
   *
   * Values in @a range that do not have a payload are set to @a payload. Values in the space are
   * not changed.
   */
  self_type &fill(range_type const &range, PAYLOAD const &payload);

  /** Find the payload at @a metric.
   *
   * @param metric The metric for which to search.
   * @return An iterator for the item or the @c end iterator if not.
   */
  iterator find(METRIC const &metric);

  /** Find the payload at @a metric.
   *
   * @param metric The metric for which to search.
   * @return An iterator for the item or the @c end iterator if not.
   */
  const_iterator find(METRIC const &metric) const;

  /** Lower bound.
   *
   * @param m Search value.
   * @return Rightmost range that starts at or before @a m.
   */
  iterator lower_bound(METRIC const &m);

  /** Upper bound.
   *
   * @param m search value
   * @return Leftmost range that starts after @a m.
   */
  iterator upper_bound(METRIC const &m);

  /** Intersection of @a range with container.
   *
   * @param range Search range.
   * @return Iterator pair that contains all ranges that intersect with @a range.
   *
   * @see lower_bound
   * @see upper_bound
   */
  std::pair<iterator, iterator> intersection(range_type const &range);

  /// @return The number of distinct ranges.
  size_t count() const;

  /// @return @c true if there are no ranges in the container, @c false otherwise.
  bool empty() const;

  /// @return Iterator for the first range.
  iterator
  begin() {
    return _list.begin();
  }
  /// @return Iterator past the last node.
  iterator
  end() {
    return _list.end();
  }
  /// @return Iterator for the first range.
  const_iterator
  begin() const {
    return _list.begin();
  }
  /// @return Iterator past the last node.
  const_iterator
  end() const {
    return _list.end();
  }

  /// Remove all ranges.
  void clear();

protected:
  /** Find the lower bounding node.
   *
   * @param target Search value.
   * @return The rightmost range that starts at or before @a target, or @c nullptr if all ranges start
   * after @a target.
   */
  Node *lower_node(METRIC const &target);

  /** Find the upper bound node.
   *
   * @param target Search value.
   * @return The leftmoswt range that starts after @a target, or @c nullptr if all ranges start
   * before @a target.
   */
  Node *upper_node(METRIC const &target);

  /// @return First node in the tree.
  Node *head();

  /// @return Last node in the tree.
  Node *tail();

  /** Insert @a node before @a spot.
   *
   * @param spot Target node.
   * @param node Node to insert.
   */
  void insert_before(Node *spot, Node *node);

  /** Insert @a node after @a spot.
   *
   * @param spot Target node.
   * @param node Node to insert.
   */
  void insert_after(Node *spot, Node *node);

  /** Add @a node to tree as the first element.
   *
   * @param node Node to prepend.
   *
   * Invariant - @a node is first in order.
   */
  void prepend(Node *node);

  /** Add @a node to tree as the last node.
   *
   * @param node Node to append.
   *
   * Invariant - @a node is last in order.
   */
  void append(Node *node);

  /** Remove node from container and update container.
   *
   * @param node Node to remove.
   */
  void remove(Node *node);
};

// ---

template <typename METRIC, typename PAYLOAD>
PAYLOAD &
DiscreteSpace<METRIC, PAYLOAD>::Node::payload() {
  return _payload;
}

template <typename METRIC, typename PAYLOAD>
auto
DiscreteSpace<METRIC, PAYLOAD>::Node::assign(DiscreteSpace::range_type const &range) -> self_type & {
  _range = range;
  return *this;
}

template <typename METRIC, typename PAYLOAD>
auto
DiscreteSpace<METRIC, PAYLOAD>::Node::assign(PAYLOAD const &payload) -> self_type & {
  _payload = payload;
  return *this;
}

template <typename METRIC, typename PAYLOAD>
void
DiscreteSpace<METRIC, PAYLOAD>::Node::structure_fixup() {
  // Invariant: The hulls of all children are correct.
  if (_left && _right) {
    // If both children, local range must be inside the hull of the children and irrelevant.
    _hull.assign(this->left()->_hull.min(), this->right()->_hull.max());
  } else if (_left) {
    _hull.assign(this->left()->_hull.min(), _range.max());
  } else if (_right) {
    _hull.assign(_range.min(), this->right()->_hull.max());
  } else {
    _hull = _range;
  }
}

// ---
// Discrete Space
// ---

template <typename METRIC, typename PAYLOAD> DiscreteSpace<METRIC, PAYLOAD>::~DiscreteSpace() {
  // Destruct all the payloads - the nodes themselves are in the arena and disappear with it.
  for (auto &node : _list) {
    std::destroy_at(&node.payload());
  }
}

template <typename METRIC, typename PAYLOAD>
size_t
DiscreteSpace<METRIC, PAYLOAD>::count() const {
  return _list.count();
}

template <typename METRIC, typename PAYLOAD>
bool
DiscreteSpace<METRIC, PAYLOAD>::empty() const {
  return _list.empty();
}

template <typename METRIC, typename PAYLOAD>
auto
DiscreteSpace<METRIC, PAYLOAD>::head() -> Node * {
  return static_cast<Node *>(_list.head());
}

template <typename METRIC, typename PAYLOAD>
auto
DiscreteSpace<METRIC, PAYLOAD>::tail() -> Node * {
  return static_cast<Node *>(_list.tail());
}

template <typename METRIC, typename PAYLOAD>
auto
DiscreteSpace<METRIC, PAYLOAD>::find(METRIC const &metric) -> iterator {
  auto n = _root; // current node to test.
  while (n) {
    if (metric < n->min()) {
      if (n->_hull.contains(metric)) {
        n = n->left();
      } else {
        return this->end();
      }
    } else if (n->max() < metric) {
      if (n->_hull.contains(metric)) {
        n = n->right();
      } else {
        return this->end();
      }
    } else {
      return _list.iterator_for(n);
    }
  }
  return this->end();
}

template <typename METRIC, typename PAYLOAD>
auto
DiscreteSpace<METRIC, PAYLOAD>::find(METRIC const &metric) const -> const_iterator {
  return const_cast<self_type *>(this)->find(metric);
}

template <typename METRIC, typename PAYLOAD>
auto
DiscreteSpace<METRIC, PAYLOAD>::lower_node(METRIC const &target) -> Node * {
  Node *n    = _root;   // current node to test.
  Node *zret = nullptr; // best node so far.

  // Fast check for sequential insertion
  if (auto ln = _list.tail(); ln != nullptr && ln->max() < target) {
    return ln;
  }

  while (n) {
    if (target < n->min()) {
      n = left(n);
    } else {
      zret = n; // this is a better candidate.
      if (n->max() < target) {
        n = right(n);
      } else {
        break;
      }
    }
  }
  return zret;
}

template <typename METRIC, typename PAYLOAD>
auto
DiscreteSpace<METRIC, PAYLOAD>::upper_node(METRIC const &target) -> Node * {
  Node *n    = _root;   // current node to test.
  Node *zret = nullptr; // best node so far.

  // Fast check if there is no range past @a target.
  if (auto ln = _list.tail(); ln == nullptr || ln->min() <= target) {
    return nullptr;
  }

  while (n) {
    if (target > n->min()) {
      n = right(n);
    } else {
      zret = n; // this is a better candidate.
      if (n->min() > target) {
        n = left(n);
      } else {
        break;
      }
    }
  }

  // Must be past any intersecting range due to STL iteration being half open.
  if (zret && (zret->min() <= target)) {
    zret = next(zret);
  }

  return zret;
}

template <typename METRIC, typename PAYLOAD>
auto
DiscreteSpace<METRIC, PAYLOAD>::lower_bound(METRIC const &m) -> iterator {
  auto n = this->lower_node(m);
  return n ? iterator(n) : this->end();
}

template <typename METRIC, typename PAYLOAD>
auto
DiscreteSpace<METRIC, PAYLOAD>::upper_bound(METRIC const &m) -> iterator {
  auto n = this->upper_node(m);
  return n ? iterator(n) : this->end();
}

template <typename METRIC, typename PAYLOAD>
auto
DiscreteSpace<METRIC, PAYLOAD>::intersection(DiscreteSpace::range_type const &range) -> std::pair<iterator, iterator> {
  // Quick checks for null intersection
  if (this->head() == nullptr ||                  // empty
      this->head()->_range.min() > range.max() || // before first range
      this->tail()->_range.max() < range.min()    // after last range
  ) {
    return {this->end(), this->end()};
  }
  // Invariant - the search range intersects the convex hull of the ranges. This doesn't require the search
  // range to intersect any of the actual ranges.

  auto *lower = this->lower_node(range.min());
  auto *upper = this->upper_node(range.max());

  if (lower == nullptr) {
    lower = this->head();
  } else if (lower->_range.max() < range.min()) {
    lower = next(lower);                     // lower is before @a range so @c next must be at or past.
    if (lower->_range.min() > range.max()) { // @a range is in an uncolored gap.
      return {this->end(), this->end()};
    }
  }

  return {_list.iterator_for(lower), upper ? _list.iterator_for(upper) : this->end()};
}

template <typename METRIC, typename PAYLOAD>
void
DiscreteSpace<METRIC, PAYLOAD>::prepend(DiscreteSpace::Node *node) {
  if (!_root) {
    _root = node;
  } else {
    _root = static_cast<Node *>(_list.head()->set_child(node, Direction::LEFT)->rebalance_after_insert());
  }
  _list.prepend(node);
}

template <typename METRIC, typename PAYLOAD>
void
DiscreteSpace<METRIC, PAYLOAD>::append(DiscreteSpace::Node *node) {
  if (!_root) {
    _root = node;
  } else {
    // The last node has no right child, or it wouldn't be the last.
    _root = static_cast<Node *>(_list.tail()->set_child(node, Direction::RIGHT)->rebalance_after_insert());
  }
  _list.append(node);
}

template <typename METRIC, typename PAYLOAD>
void
DiscreteSpace<METRIC, PAYLOAD>::remove(DiscreteSpace::Node *node) {
  _root = static_cast<Node *>(node->remove());
  _list.erase(node);
  _fa.destroy(node);
}

template <typename METRIC, typename PAYLOAD>
void
DiscreteSpace<METRIC, PAYLOAD>::insert_before(DiscreteSpace::Node *spot, DiscreteSpace::Node *node) {
  if (left(spot) == nullptr) {
    spot->set_child(node, Direction::LEFT);
  } else {
    // If there's a left child, there's a previous node, therefore spot->_prev is valid.
    // Further, the previous node must be the rightmost descendant node of the left subtree
    // and therefore has no current right child.
    spot->_prev->set_child(node, Direction::RIGHT);
  }

  _list.insert_before(spot, node);
  _root = static_cast<Node *>(node->rebalance_after_insert());
}

template <typename METRIC, typename PAYLOAD>
void
DiscreteSpace<METRIC, PAYLOAD>::insert_after(DiscreteSpace::Node *spot, DiscreteSpace::Node *node) {
  if (right(spot) == nullptr) {
    spot->set_child(node, Direction::RIGHT);
  } else {
    // If there's a right child, there's a successor node, and therefore @a _next is valid.
    // Further, the successor node must be the left most descendant of the right subtree
    // therefore it doesn't have a left child.
    spot->_next->set_child(node, Direction::LEFT);
  }

  _list.insert_after(spot, node);
  _root = static_cast<Node *>(node->rebalance_after_insert());
}

template <typename METRIC, typename PAYLOAD>
DiscreteSpace<METRIC, PAYLOAD> &
DiscreteSpace<METRIC, PAYLOAD>::erase(DiscreteSpace::range_type const &range) {
  Node *n = this->lower_node(range.min()); // current node.
  while (n) {
    auto nn = next(n);            // cache in case @a n disappears.
    if (n->min() > range.max()) { // cleared the target range, done.
      break;
    }

    if (n->max() >= range.min()) {     // some overlap
      if (n->max() <= range.max()) {   // pure left overlap, clip.
        if (n->min() >= range.min()) { // covered, remove.
          this->remove(n);
        } else { // stub on the left, clip to that.
          n->assign_max(--metric_type{range.min()});
        }
      } else if (n->min() >= range.min()) { // pure left overlap, clip.
        n->assign_min(++metric_type{range.max()});
      } else { // @a n covers @a range, must split.
        auto y = _fa.make(range_type{n->min(), --metric_type{range.min()}}, n->payload());
        n->assign_min(++metric_type{range.max()});
        this->insert_before(n, y);
        break;
      }
    }
    n = nn;
  }
  return *this;
}

template <typename METRIC, typename PAYLOAD>
DiscreteSpace<METRIC, PAYLOAD> &
DiscreteSpace<METRIC, PAYLOAD>::mark(DiscreteSpace::range_type const &range, PAYLOAD const &payload) {
  Node *n = this->lower_node(range.min()); // current node.
  Node *x = nullptr;                       // New node, gets set if we re-use an existing one.
  Node *y = nullptr;                       // Temporary for removing and advancing.

  // Use carefully, only in situations where it is known there is no overflow.
  auto max_plus_1 = ++metric_type{range.max()};

  /*  We have lots of special cases here primarily to minimize memory
      allocation by re-using an existing node as often as possible.
  */
  if (n) {
    // Watch for wrap.
    auto min_minus_1 = --metric_type{range.min()};
    if (n->min() == range.min()) {
      // Could be another span further left which is adjacent.
      // Coalesce if the data is the same. min_minus_1 is OK because
      // if there is a previous range, min is not zero.
      Node *p = prev(n);
      if (p && p->payload() == payload && p->max() == min_minus_1) {
        x = p;
        n = x; // need to back up n because frame of reference moved.
        x->assign_max(range.max());
      } else if (n->max() <= range.max()) {
        // Span will be subsumed by request span so it's available for use.
        x = n;
        x->assign_max(range.max()).assign(payload);
      } else if (n->payload() == payload) {
        return *this; // request is covered by existing span with the same data
      } else {
        // request span is covered by existing span.
        x = _fa.make(range, payload); //
        n->assign_min(max_plus_1);    // clip existing.
        this->insert_before(n, x);
        return *this;
      }
    } else if (n->payload() == payload && n->max() >= min_minus_1) {
      // min_minus_1 is safe here because n->min() < min so min is not zero.
      x = n;
      // If the existing span covers the requested span, we're done.
      if (x->max() >= range.max()) {
        return *this;
      }
      x->assign_max(range.max());
    } else if (n->max() <= range.max()) {
      // Can only have left skew overlap, otherwise disjoint.
      // Clip if overlap.
      if (n->max() >= range.min()) {
        n->assign_max(min_minus_1);
      } else if (nullptr != (y = next(n)) && y->max() <= range.max()) {
        // because @a n was selected as the minimum it must be the case that
        // y->min >= min (or y would have been selected). Therefore in this
        // case the request covers the next node therefore it can be reused.
        x = y;
        x->assign(range).assign(payload).ripple_structure_fixup();
        n = x; // this gets bumped again, which is correct.
      }
    } else {
      // Existing span covers new span but with a different payload.
      // We split it, put the new span in between and we're done.
      // max_plus_1 is valid because n->max() > max.
      Node *r;
      x = _fa.make(range, payload);
      r = _fa.make(range_type{max_plus_1, n->max()}, n->payload());
      n->assign_max(min_minus_1);
      this->insert_after(n, x);
      this->insert_after(x, r);
      return *this; // done.
    }
    n = next(n); // lower bound span handled, move on.
    if (!x) {
      x = _fa.make(range, payload);
      if (n) {
        this->insert_before(n, x);
      } else {
        this->append(x); // note that since n == 0 we'll just return.
      }
    }
  } else if (nullptr != (n = this->head()) &&                    // at least one node in tree.
             n->payload() == payload &&                          // payload matches
             (n->max() <= range.max() || n->min() <= max_plus_1) // overlap or adj.
  ) {
    // Same payload with overlap, re-use.
    x = n;
    n = next(n);
    x->assign_min(range.min());
    if (x->max() < range.max()) {
      x->assign_max(range.max());
    }
  } else {
    x = _fa.make(range, payload);
    this->prepend(x);
  }

  // At this point, @a x has the node for this span and all existing spans of
  // interest start at or past this span.
  while (n) {
    if (n->max() <= range.max()) { // completely covered, drop span, continue
      y = n;
      n = next(n);
      this->remove(y);
    } else if (max_plus_1 < n->min()) { // no overlap, done.
      break;
    } else if (n->payload() == payload) { // skew overlap or adj., same payload
      x->assign_max(n->max());
      y = n;
      n = next(n);
      this->remove(y);
    } else if (n->min() <= range.max()) { // skew overlap different payload
      n->assign_min(max_plus_1);
      break;
    } else { // n->min() > range.max(), different payloads - done.
      break;
    }
  }

  return *this;
}

template <typename METRIC, typename PAYLOAD>
DiscreteSpace<METRIC, PAYLOAD> &
DiscreteSpace<METRIC, PAYLOAD>::fill(DiscreteSpace::range_type const &range, PAYLOAD const &payload) {
  // Rightmost node of interest with n->min() <= min.
  Node *n = this->lower_node(range.min());
  Node *x = nullptr; // New node (if any).
  // Need copies because we will modify these.
  auto min = range.min();
  auto max = range.max();

  // Handle cases involving a node of interest to the left of the
  // range.
  if (n) {
    if (n->min() < min) {
      auto min_1 = min;
      --min_1;                // dec is OK because min isn't zero.
      if (n->max() < min_1) { // no overlap, not adjacent.
        n = next(n);
      } else if (n->max() >= max) { // incoming range is covered, just discard.
        return *this;
      } else if (n->payload() != payload) { // different payload, clip range on left.
        min = n->max();
        ++min;
        n = next(n);
      } else { // skew overlap or adjacent to predecessor with same payload, use node and continue.
        x = n;
        n = next(n);
      }
    }
  } else {
    n = this->head();
  }

  // Work through the rest of the nodes of interest.
  // Invariant: n->min() >= min

  // Careful here -- because max_plus1 might wrap we need to use it only if we can be certain it
  // didn't. This is done by ordering the range tests so that when max_plus1 is used when we know
  // there exists a larger value than max.
  auto max_plus1 = max;
  ++max_plus1;

  /* Notes:
     - max (and thence also max_plus1) never change during the loop.
     - we must have either x != 0 or adjust min but not both for each loop iteration.
  */
  while (n) {
    if (n->payload() == payload) {
      if (x) {
        if (n->max() <= max) { // next range is covered, so we can remove and continue.
          this->remove(n);
          n = next(x);
        } else if (n->min() <= max_plus1) {
          // Overlap or adjacent with larger max - absorb and finish.
          x->assign_max(n->max());
          this->remove(n);
          return *this;
        } else {
          // have the space to finish off the range.
          x->assign_max(max);
          return *this;
        }
      } else {                 // not carrying a span.
        if (n->max() <= max) { // next range is covered - use it.
          x = n;
          x->assign_min(min);
          n = next(n);
        } else if (n->min() <= max_plus1) {
          n->assign_min(min);
          return *this;
        } else { // no overlap, space to complete range.
          this->insert_before(n, _fa.make(min, max, payload));
          return *this;
        }
      }
    } else { // different payload
      if (x) {
        if (max < n->min()) { // range ends before n starts, done.
          x->assign_max(max);
          return *this;
        } else if (max <= n->max()) { // range ends before n, done.
          x->assign_max(--metric_type(n->min()));
          return *this;
        } else { // n is contained in range, skip over it.
          x->assign_max(--metric_type(n->min()));
          x   = nullptr;
          min = n->max();
          ++min; // OK because n->max() maximal => next is null.
          n = next(n);
        }
      } else {                // no carry node.
        if (max < n->min()) { // entirely before next span.
          this->insert_before(n, _fa.make(min, max, payload));
          return *this;
        } else {
          if (min < n->min()) { // leading section, need node.
            auto y = _fa.make(min, --metric_type(n->min()), payload);
            this->insert_before(n, y);
          }
          if (max <= n->max()) { // nothing past node
            return *this;
          }
          min = n->max();
          ++min;
          n = next(n);
        }
      }
    }
  }
  // Invariant: min is larger than any existing range maximum.
  if (x) {
    x->assign_max(max);
  } else {
    this->append(_fa.make(min, max, payload));
  }
  return *this;
}

template <typename METRIC, typename PAYLOAD>
template <typename F, typename U>
auto
DiscreteSpace<METRIC, PAYLOAD>::blend(DiscreteSpace::range_type const &range, U const &color, F &&blender) -> self_type & {
  // Do a base check for the color to use on unmapped values. If self blending on @a color
  // is @c false, then do not color currently unmapped values.
  PAYLOAD plain_color{};                            // color to paint uncolored metrics.
  bool plain_color_p = blender(plain_color, color); // start with default and blend in @a color.

  auto node_cleaner = [&](Node *ptr) -> void { _fa.destroy(ptr); };
  // Used to hold a temporary blended node - @c release if put in space, otherwise cleaned up.
  using unique_node = std::unique_ptr<Node, decltype(node_cleaner)>;

  // Rightmost node of interest with n->min() <= range.min().
  Node *n = this->lower_node(range.min());

  // This doesn't change, compute outside loop.
  auto range_max_plus_1 = range.max();
  ++range_max_plus_1; // only use in contexts where @a max < METRIC max value.

  // Update every loop to track what remains to be filled.
  auto remaining = range;

  if (nullptr == n) {
    n = this->head();
  }

  // Process @a n, covering the values from the previous range to @a n.max
  while (n) {
    // If there's no overlap, skip because this will be checked next loop, or it's the last node
    // and will be checked post loop. Loop logic is simpler if n->max() >= remaining.min()
    if (n->max() < remaining.min()) {
      n = next(n);
      continue;
    }
    // Invariant - n->max() >= remaining.min();

    // Check for left extension. If found, clip that node to be adjacent and put in a
    // temporary that covers the overlap with the original payload.
    if (n->min() < remaining.min()) {
      // @a fill is inserted iff n->max() < remaining.max(), in which case the max is correct.
      // This is needed in other cases only for the color blending result.
      unique_node fill{_fa.make(remaining.min(), n->max(), n->payload()), node_cleaner};
      bool fill_p = blender(fill->payload(), color); // fill or clear?

      if (fill_p) {
        bool same_color_p = fill->payload() == n->payload();
        if (same_color_p && n->max() >= remaining.max()) {
          return *this; // incoming range is completely covered by @a n in the same color, done.
        }
        if (!same_color_p) {
          auto fn    = fill.release();
          auto n_max = n->max();                         // save this so @a n can be clipped.
          n->assign_max(--metric_type(remaining.min())); // clip @a n down.
          this->insert_after(n, fn);                     // add intersection node in different color.
          if (n_max > remaining.max()) {                 // right extent too - split and done.
            fn->assign_max(remaining.max());             // fill node stops at end of target range.
            this->insert_after(fn, _fa.make(++metric_type(remaining.max()), n_max, n->payload()));
            return *this;
          }
          n = fn; // skip to use new node as current node.
        }
        remaining.assign_min(++metric_type(n->max())); // going to fill up n->max(), clip target.
      } else {                                         // clear, don't fill.
        auto n_r = n->range();                         // cache to avoid ordering requirements.
        if (n_r.max() > remaining.max()) {             // overhang on the right, must split.
          fill.release();                              // not going to use it,
          n->assign_min(++metric_type(remaining.max()));
          this->insert_before(n, _fa.make(n_r.min(), --metric_type(remaining.min()), n->payload()));
          return *this;
        }
        n->assign_max(--metric_type(remaining.min())); // clip @a n down.
        if (n_r.max() == remaining.max()) {
          return *this;
        }
        remaining.assign_min(++metric_type(n_r.max()));
      }
      continue;
    }

    Node *pred = prev(n);
    // invariant
    // - remaining.min() <= n->max()
    // - !pred || pred->max() < remaining.min()

    // Calculate and cache key relationships between @a n and @a remaining.

    // @a n extends past @a remaining, so the trailing segment must be dealt with.
    bool right_ext_p = n->max() > remaining.max();
    // @a n strictly right overlaps with @a remaining.
    bool right_overlap_p = remaining.contains(n->min());
    // @a n is adjacent on the right to @a remaining.
    bool right_adj_p = !right_overlap_p && remaining.is_left_adjacent_to(n->range());
    // @a n has the same color as would be used for unmapped values.
    bool n_plain_colored_p = plain_color_p && (n->payload() == plain_color);

    // Check for no right overlap - that means @a n is past the target range.
    // It may be possible to extend @a n or the previous range to cover
    // the target range. Regardless, all of @a range can be filled at this point.
    if (!right_overlap_p) {
      // @a pred has the same color as would be used for unmapped values
      // and is adjacent to @a remaining.
      bool pred_plain_colored_p = pred && ++metric_type(pred->max()) == remaining.min() && pred->payload() == plain_color;

      if (right_adj_p && n_plain_colored_p) { // can pull @a n left to cover
        n->assign_min(remaining.min());
        if (pred_plain_colored_p) { // if that touches @a pred with same color, collapse.
          auto pred_min = pred->min();
          this->remove(pred);
          n->assign_min(pred_min);
        }
      } else if (pred_plain_colored_p) { // can pull @a pred right to cover.
        pred->assign_max(remaining.max());
      } else if (!remaining.empty() && plain_color_p) { // Must add new range if plain color valid.
        this->insert_before(n, _fa.make(remaining.min(), remaining.max(), plain_color));
      }
      return *this;
    }

    // Invariant: @n has right overlap with @a remaining

    // If there's a gap on the left, fill from @a r.min to @a n.min - 1
    if (plain_color_p && remaining.min() < n->min()) {
      if (n->payload() == plain_color) {
        if (pred && pred->payload() == n->payload()) {
          auto pred_min{pred->min()};
          this->remove(pred);
          n->assign_min(pred_min);
        } else {
          n->assign_min(remaining.min());
        }
      } else {
        auto n_min_minus_1{n->min()};
        --n_min_minus_1;
        if (pred && pred->payload() == plain_color) {
          pred->assign_max(n_min_minus_1);
        } else {
          this->insert_before(n, _fa.make(remaining.min(), n_min_minus_1, plain_color));
        }
      }
    }

    // Invariant: Space in @a range and to the left of @a n has been filled.

    // Create a node with the blend for the overlap and then update / replace @a n as needed.
    auto max{right_ext_p ? remaining.max() : n->max()}; // smallest boundary of range and @a n.
    unique_node fill{_fa.make(n->min(), max, n->payload()), node_cleaner};
    bool fill_p = blender(fill->payload(), color); // fill or clear?
    auto next_n = next(n);                         // cache this in case @a n is removed.
    remaining.assign_min(++METRIC{fill->max()});   // Update what is left to fill.

    // Clean up the range for @a n
    if (fill_p) {
      // Check if @a pred is suitable for extending right to cover the target range.
      bool pred_adj_p =
        nullptr != (pred = prev(n)) && pred->range().is_left_adjacent_to(fill->range()) && pred->payload() == fill->payload();

      if (right_ext_p) {
        if (n->payload() == fill->payload()) {
          n->assign_min(fill->min());
        } else {
          n->assign_min(range_max_plus_1);
          if (pred_adj_p) {
            pred->assign_max(fill->max());
          } else {
            this->insert_before(n, fill.release());
          }
        }
        return *this; // @a n extends past @a remaining, -> all done.
      } else {
        // Collapse in to previous range if it's adjacent and the color matches.
        if (pred_adj_p) {
          this->remove(n);
          pred->assign_max(fill->max());
        } else {
          this->insert_before(n, fill.release());
          this->remove(n);
        }
      }
    } else if (right_ext_p) {
      n->assign_min(range_max_plus_1);
      return *this;
    } else {
      this->remove(n);
    }

    // Everything up to @a n.max is correct, time to process next node.
    n = next_n;
  }

  // Arriving here means there are no more ranges past @a range (those cases return from the loop).
  // Therefore the final fill node is always last in the tree.
  if (plain_color_p && !remaining.empty()) {
    // Check if the last node can be extended to cover because it's left adjacent.
    // Can decrement @a range_min because if there's a range to the left, @a range_min is not minimal.
    n = _list.tail();
    if (n && n->max() >= --metric_type{remaining.min()} && n->payload() == plain_color) {
      n->assign_max(range.max());
    } else {
      this->append(_fa.make(remaining.min(), remaining.max(), plain_color));
    }
  }

  return *this;
}

template <typename METRIC, typename PAYLOAD>
void
DiscreteSpace<METRIC, PAYLOAD>::clear() {
  for (auto &node : _list) {
    std::destroy_at(&node.payload());
  }
  _list.clear();
  _root = nullptr;
  _arena.clear();
  _fa.clear();
}

}} // namespace swoc::SWOC_VERSION_NS
