/**
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

  Copyright 2020 Verizon Media
*/

#pragma once

#include <iostream>
#include <utility>
#include <stack>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <type_traits>
#include <cassert>

#include <swoc/TextView.h>
#include <swoc/swoc_meta.h>

// fwd declarations.
class Comparison;
template <class T> class reversed_view;

///
/// @brief PATRICA algorithm implementation using binary trees.
///        This Data Structure allows to search for N key in exactly N nodes, providing a log(N) bit
///        comparision with a single full key comparision per search. The key(or view) is stored in the node,
///        nodes are traversed according to the bits of the key, this implementation does NOT uses the key
///        while traversing, it only stores it for a possible letter reference when the end of the tree/search
///        is reached and the full match needs to be granted.
///
/// @note We only support insert, full match and prefix match.
/// @note To handle suffix_match please refer to @c reversed_view<T>
/// @tparam Key
/// @tparam Value
///
template <typename Key, typename Value> class StringTree
{
  /// haven't test more than this types.
  static_assert(swoc::meta::is_any_of<Key, std::string, std::string_view, swoc::TextView, reversed_view<swoc::TextView>>::value,
                "Type not supported");
  // static_assert(swoc::meta::is_any_of_v<Key, std::string, std::string_view, swoc::TextView, reversed_view<swoc::TextView>>,
  //               "Type not supported");
  using self_type = StringTree<Key, Value>;
  /// Node layout.
  struct Node {
    // types
    using self_type = Node;
    using ptr_type  = self_type *;

    Node(Key k, Value v, int32_t r = -1) : key{std::move(k)}, value{std::move(v)}, rank{r}
    {
      left  = this;
      right = this;
    }
    Key key;
    Value value;
    /// bit pos where it differs from previous node.
    std::size_t bit_count{0};
    /// key/value rank.
    int32_t rank;
    /// only two ways, btree
    self_type *left;
    self_type *right;

    Node()                        = delete;
    Node(Node const &)            = delete;
    Node(Node &&)                 = delete;
    Node &operator=(Node const &) = delete;
    Node &operator=(Node &&)      = delete;
  };
  // types
  using node_type     = Node;
  using node_type_ptr = node_type *;

public:
  // types
  using value_type = Value;
  using key_type   = Key;

  /// Construct a new String Tree object
  StringTree();

  /// We have a bunch of memory to clean up after use.
  ~StringTree();

  StringTree(StringTree &&)                 = delete;
  StringTree(StringTree const &)            = delete;
  StringTree &operator=(StringTree const &) = delete;
  StringTree &operator=(StringTree &&)      = delete;

  ///
  /// @brief  Inserts element into the tree, if the container doesn't already contain an element with an equivalent key.
  /// @return true if the k/v was properly inserted, false if not.
  ///
  bool insert(Key const &key, Value const &value, Comparison *cmp = nullptr);
  ///
  /// @brief  Finds an element with equivalent key. Only full match.
  /// @return A pair with a boolean indicating if the key was found and the value associated with it.
  ///         If value was not found, value can be ignored
  std::pair<bool, Value> full_match(Key const &key, Comparison *cmp = nullptr) const noexcept;
  ///
  /// @brief  Find a value(s) associated with a prefix of a key, this function will perform a prefix search
  ///         only among all the related keys in the tree.
  /// @return list of pairs with all the found matches.
  ///
  /// TODO: this needs to be changed and use rank to return only 1 value.
  std::vector<std::pair<Key, Value>> prefix_match(Key const &prefix, Comparison *cmp = nullptr) const;

private:
  /// hold the first element on the entire tree.
  node_type_ptr _head;
  /// this gets incremented on every insert.
  int32_t _rank_counter{0};
  /// recursive memory cleanup function.
  void freeup(Node *n);
  /// clean up function, should deal with all the crap.
  void cleanup();
};

/// --------------------------------------------------------------------------------------------------------------------
namespace detail
{
// Just to make the code a bit more readable
static constexpr int BIT_ON{1};

/// @brief return a bit value from a particular position in a byte
static auto
get_bit_from_byte(char key, std::size_t position) noexcept
{
  int const position_in_byte = position - ((position / 8) * 8);
  return ((key) >> (7 - position_in_byte)) & 1;
}

/// @brief Specialization to deal with suffix and prefix byte getter.
template <typename T>
auto
get_byte(typename T::const_pointer ptr, int byte_number,
         typename std::enable_if_t<std::is_same_v<T, reversed_view<swoc::TextView>>> *val = 0)
{
  typename T::const_pointer byte = ptr - byte_number;
  return *byte;
}
template <typename T>
auto
get_byte(typename T::const_pointer ptr, int byte_number,
         typename std::enable_if_t<!std::is_same_v<T, reversed_view<swoc::TextView>>> *val = 0)
{
  typename T::const_pointer byte = ptr + byte_number;
  return *byte;
}

/// @brief Get a specific bit position from a stream of bytes.
template <typename Key>
static auto
get_bit(Key const &key, std::size_t position)
{
  if (position / 8 >= key.size()) {
    return 0;
  }
  typename Key::const_pointer ptr = &*std::begin(key);
  assert(ptr != nullptr);
  int const byte_number      = position / 8;
  int const position_in_byte = position - (byte_number * 8);
  auto byte                  = get_byte<Key>(ptr, byte_number);
  return ((byte) >> (7 - position_in_byte)) & 1;
}

template <typename Key>
static auto
get_first_diff_bit_position(Key const &lhs, Key const &rhs)
{
  std::size_t byte_count{0};
  auto lhs_iter = std::begin(lhs);
  auto rhs_iter = std::begin(rhs);
  while ((lhs_iter != std::end(lhs) && rhs_iter != std::end(rhs)) && *lhs_iter == *rhs_iter) {
    ++lhs_iter;
    ++rhs_iter;
    byte_count++;
  }

  std::size_t bit_diff_count{0};

  for (auto bit_pos = 0; bit_pos < 8; ++bit_pos, ++bit_diff_count) {
    if (get_bit_from_byte(*lhs_iter, bit_pos) != get_bit_from_byte(*rhs_iter, bit_pos)) {
      break;
    }
  }

  return (byte_count << 3) + bit_diff_count;
}
} // namespace detail

/// --------  StringTree implementation -------------

template <typename Key, typename Value> StringTree<Key, Value>::StringTree()
{
  // We do not set the rank now, as for the head, -1 should be ok.
  // we may be able to no need to initialize empty string if Key=string_view, thinking about this
  // as isPrefix may badly fail if null. Maybe check for head(before call to isPrefix).
  _head            = new Node(Key{""}, Value{});
  _head->bit_count = 0;
}

template <typename Key, typename Value> StringTree<Key, Value>::~StringTree()
{
  cleanup();
}

template <typename Key, typename Value>
bool
StringTree<Key, Value>::insert(Key const &key, Value const &value, Comparison *cmp)
{
  node_type_ptr search_node = _head;
  std::size_t idx{0};

  // We wil try to go down the path and get close to the place where we want to insert the new value, then we will
  // follow the logic as like a search miss.
  do {
    idx         = search_node->bit_count;
    search_node = (detail::get_bit(key, search_node->bit_count) == detail::BIT_ON ? search_node->right : search_node->left);
  } while (idx < search_node->bit_count);

  // In case it's already here.
  if (key == search_node->key) {
    return false;
  }

  // Get the first bit position that differs
  std::size_t const first_diff_bit = (search_node == _head ? 1 : detail::get_first_diff_bit_position(key, search_node->key));

  // Getting ready the new node.
  node_type_ptr new_node = new Node(key, value, _rank_counter++);

  // we will work on this two from now. Always left to start.
  node_type_ptr p = _head;
  node_type_ptr c = _head->left;
  // with the bit pos diff calculated on the search miss above, we now need to go down (c > p) using the bit diff.
  // first_diff_bit will tell us when to stop and insert the key.
  while (c->bit_count > p->bit_count && first_diff_bit > c->bit_count) {
    p = c;
    c = (detail::get_bit(key, c->bit_count) == detail::BIT_ON ? c->right : c->left);
  }
  // set up the new node and link it.
  new_node->bit_count = first_diff_bit;
  int const bit       = detail::get_bit(key, first_diff_bit);
  // we pick the side base on the bit value. If 0 then left, 1 goes to right node.
  new_node->left  = bit == detail::BIT_ON ? c : new_node;
  new_node->right = bit == detail::BIT_ON ? new_node : c;

  if (detail::get_bit(key, p->bit_count) == detail::BIT_ON) {
    p->right = new_node;
  } else {
    p->left = new_node;
  }

  // new key/values ok.
  return true;
}

template <typename Key, typename Value>
std::pair<bool, Value>
StringTree<Key, Value>::full_match(Key const &key, Comparison *cmp) const noexcept
{
  node_type_ptr search_node = _head->left;
  std::size_t idx{0};

  // Walk down the tree using the bit_count to check which direction take. We will check this on every node.
  // If 1 we follow right, on 0 we go left. We will stop when find the uplink. At this point only we will compare the key.
  do {
    idx         = search_node->bit_count;
    search_node = detail::get_bit(key, search_node->bit_count) == detail::BIT_ON ? search_node->right : search_node->left;
  } while (idx < search_node->bit_count);

  // cmp
  const bool found = search_node->key == key;
  // either the requested value, or an empty value (it happens that we have one already at _head).
  return {found, found ? search_node->value : _head->value};
}

template <typename Key, typename Value>
std::vector<std::pair<Key, Value>>
StringTree<Key, Value>::prefix_match(Key const &prefix, Comparison *cmp) const
{
  // Nodes will help to follow, but basically we:
  // 1 - find the closest node.
  // 2 - with the closest node, we walk down the tree till we find an uplink. DFS is performed.
  // 3 - downlinks and the first uplink will be check for prefix.

  // helper to check prefix on two strings.
  auto const is_prefix = [](Key const &prefix, Key const &value) {
    return std::mismatch(std::begin(prefix), std::end(prefix), std::begin(value)).first == std::end(prefix);
  };

  // For now we return the entire list, we need to work on the rank to send back only one match, the best one.
  std::size_t const prefix_size{(prefix.size() * 8) - 1};

  node_type_ptr root = _head->left;

  std::size_t children_bit_count{0};
  // We need to find the node where we will start the prefix lookup. Move down using prefix size.
  while (prefix_size > root->bit_count && root->bit_count > children_bit_count) {
    children_bit_count = root->bit_count;
    root               = detail::get_bit(prefix, root->bit_count) == detail::BIT_ON ? root->right : root->left;
  }

  std::vector<std::pair<Key, Value>> search;
  // Once we have the closest node to start going down(or up), we will deal with the result as
  // this was a graph, and we will perform a "sort of" DFS till we find what we need.
  std::unordered_map<node_type_ptr, bool> visited;

  // We use DFS from the root node we found, and visit all the related (by prefix) nodes down the line, till
  // we find an uplink.
  std::stack<node_type_ptr> stack_downlinks;
  stack_downlinks.push(root);

  while (!stack_downlinks.empty()) {
    auto e = stack_downlinks.top();
    stack_downlinks.pop();
    visited[e] = true;

    auto not_seen = visited.find(e->left) == std::end(visited);
    // We have to do a special check for uplinks as it can get us into a circular list and it may make us check
    // nodes that does not belong to the prefix we are looking for. We have to run the check on both branches.
    // isPrefix will check the up-link in this case to avoid the circular issue.
    if (e->bit_count > e->left->bit_count && not_seen && is_prefix(prefix, e->left->key)) {
      search.push_back({e->left->key, e->left->value});
    }
    // Same on the right branch.
    not_seen = visited.find(e->right) == std::end(visited);
    if (e->bit_count > e->right->bit_count && not_seen && is_prefix(prefix, e->right->key)) {
      search.push_back({e->right->key, e->right->value});
    }

    if (e->bit_count <= e->left->bit_count && e != e->left) {
      stack_downlinks.push(e->left);
    }
    if (e->bit_count <= e->right->bit_count && e != e->right) {
      stack_downlinks.push(e->right);
    }

    if (is_prefix(prefix, e->key)) {
      search.push_back({e->key, e->value});
    }
  }
  return search;
}

template <typename Key, typename Value>
void
StringTree<Key, Value>::freeup(Node *n)
{
  if (n == nullptr) {
    return;
  }

  node_type_ptr left  = n->left;
  node_type_ptr right = n->right;

  if (left->bit_count >= n->bit_count && (left != n && left != _head)) {
    freeup(left);
  }
  if (right->bit_count >= n->bit_count && (right != n && right != _head)) {
    freeup(right);
  }
  // ^^ should catch the nullptr
  delete n;
}

template <typename Key, typename Value>
void
StringTree<Key, Value>::cleanup()
{
  freeup(_head);
}
/// --------------------------------------------------------------------------------------------------------------------

///
/// @brief Wrapper class to "view" a string_view/TextView as reversed view
///
/// @tparam View Original string view that needs to be stored in reverse gear.
///
template <typename View> class reversed_view
{
  /// haven't test more than this types.
  static_assert(swoc::meta::is_any_of<View, std::string, std::string_view, swoc::TextView>::value, "Type not supported");
  // TODO: remove ^^ once is_any_of_v is available.
  // static_assert(swoc::meta::is_any_of_v<View, std::string, std::string_view, swoc::TextView>, "Type not supported");

public:
  // Think, inherit from View instead??
  using const_pointer = typename View::const_pointer;
  using pointer       = typename View::pointer;
  using value_type    = typename View::value_type;

  // Define our own iterator which show a reverse view of the original.
  struct iterator {
    using reference = value_type &;
    explicit iterator(typename View::reverse_iterator iter) : _iter(iter) {}
    const value_type &
    operator*() const
    {
      return *_iter;
    }

    // TODO: Work on some operator to provide a better api (operator+, operator+=, etc)
    iterator &
    operator++()
    {
      ++_iter;
      return *this;
    }

    iterator
    operator++(int)
    {
      iterator it(*this);
      operator++();
      return it;
    }

    bool
    operator==(iterator const &lhs) const
    {
      return _iter == lhs._iter;
    }

    bool
    operator!=(iterator const &lhs) const
    {
      return _iter != lhs._iter;
    }

  private:
    typename View::reverse_iterator _iter;
  };

  reversed_view() noexcept = default;

  explicit reversed_view(View view) noexcept : _view(view) {}
  bool
  operator==(View const &v) const noexcept
  {
    return v == _view;
  }

  bool
  empty() const noexcept
  {
    return _view.empty();
  }

  iterator
  begin() const noexcept
  {
    return iterator{_view.rbegin()};
  }

  iterator
  end() const noexcept
  {
    return iterator{_view.rend()};
  }

  typename View::size_type
  size() const noexcept
  {
    return _view.size();
  }

  typename View::const_pointer
  data() const noexcept
  {
    return _view.data();
  }
  // for debugging purpose we may need to log it.
  template <typename T> friend std::ostream &operator<<(std::ostream &os, reversed_view<T> const &v);
  template <typename T> friend bool operator==(reversed_view<T> const &lhs, reversed_view<T> const &rhs);

  // To be removed. POC for testing purposes.
  View
  get_view() const noexcept
  {
    return _view;
  }

private:
  View _view;
};
namespace std
{

template <> struct iterator_traits<reversed_view<swoc::TextView>::iterator> {
  using value_type        = typename swoc::TextView::value_type;
  using pointer_type      = const value_type *;
  using reference_type    = const value_type &;
  using difference_type   = ssize_t;
  using iterator_category = bidirectional_iterator_tag;
};
} // namespace std
template <typename View>
std::ostream &
operator<<(std::ostream &os, reversed_view<View> const &v)
{
  std::for_each(v._view.rbegin(), v._view.rend(), [&os](typename View::value_type c) { os << c; });
  return os;
}

template <typename View>
bool
operator==(reversed_view<View> const &lhs, reversed_view<View> const &rhs)
{
  return lhs._view == rhs._view;
}

/// --------------------------------------------------------------------------------------------------------------------

///
/// @brief Abstraction of the string_tree implementation which can be used for:
///        full_match, prefix_match and suffix_match
///        Two instance of a string_trie are being held here, one for full/prefix search and a second one for suffix_match
///        which uses the original implementation plus a wrapper around the TextView in order to make it look like a
///        reversed string, TextView won't be modified.
///        TransformView can help?
class string_tree_map
{
public:
  using value_type  = swoc::TextView;
  using key_type    = swoc::TextView;
  using search_type = std::vector<std::pair<key_type, value_type>>;

  bool
  insert(key_type key, value_type value)
  {
    return _prefix_map.insert(key, value) && _suffix_map.insert(key, value);
  }

  std::pair<bool, value_type>
  full_match(key_type key) const noexcept
  {
    return _prefix_map.full_match(key);
  }

  std::vector<std::pair<key_type, value_type>>
  prefix_match(key_type prefix, Comparison *cmp = nullptr) const
  {
    return _prefix_map.prefix_match(prefix, cmp);
  }

  std::vector<std::pair<key_type, value_type>>
  suffix_match(key_type suffix, Comparison *cmp = nullptr) const
  {
    // Logic works, but need to tweak the internal wrapper implementation (SuffixMatchMap)
    // to be able to translate this in a cheap way. Also, as the goal is to just return 1
    // element from the tree, this may entirely go away.
    return _suffix_map.suffix_match(suffix, cmp);
  }

private:
  using PrefixAndFullMatchMap = StringTree<key_type, value_type>;
  // Suffix match search, it will show the original string as reversed.
  struct SuffixMatchMap : private StringTree<reversed_view<string_tree_map::key_type>, value_type> {
    using super = StringTree<reversed_view<string_tree_map::key_type>, value_type>;

    bool
    insert(string_tree_map::key_type key, string_tree_map::value_type value)
    {
      return super::insert(reversed_view{key}, value);
    }

    std::vector<std::pair<string_tree_map::key_type, string_tree_map::value_type>>
    suffix_match(string_tree_map::key_type suffix, Comparison *cmp) const
    {
      // This will go away, temporary hack.
      std::vector<std::pair<string_tree_map::key_type, string_tree_map::value_type>> r;
      for (auto const &p : super::prefix_match(reversed_view{suffix}, cmp)) {
        // Temporal hack, 'get_view()' need to go away.
        r.push_back({p.first.get_view(), p.second});
      }
      return r;
    }
  };

  // Only handle full_match, prefix_match;
  PrefixAndFullMatchMap _prefix_map;
  // Only handle suffix match.
  SuffixMatchMap _suffix_map;
};
