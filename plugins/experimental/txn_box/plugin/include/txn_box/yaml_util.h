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

  Copyright 2019, Oath Inc.
*/

#pragma once

#include <swoc/TextView.h>
#include <swoc/bwf_base.h>
#include <swoc/Errata.h>
#include <swoc/swoc_file.h>
#include <yaml-cpp/yaml.h>

// Structured binding support for nodes. E.g.
// YAML::Node node;
// for ( auto const& [ key, value ] : node ) { ... }
// for ( auto [ key, value ] : node) { ... }
// lvalue reference doesn't work because the iterator returns a value, not a reference.
namespace std
{
template <> class tuple_size<YAML::const_iterator::value_type> : public std::integral_constant<size_t, 2>
{
};
template <> class tuple_element<0, YAML::const_iterator::value_type>
{
public:
  using type = const YAML::Node;
};
template <> class tuple_element<1, YAML::const_iterator::value_type>
{
public:
  using type = const YAML::Node;
};

template <> class tuple_size<YAML::iterator::value_type> : public std::integral_constant<size_t, 2>
{
};
template <> class tuple_element<0, YAML::iterator::value_type>
{
public:
  using type = YAML::Node;
};
template <> class tuple_element<1, YAML::iterator::value_type>
{
public:
  using type = YAML::Node;
};
} // namespace std

template <size_t IDX> YAML::Node const &get(YAML::const_iterator::value_type const &v);
template <>
inline YAML::Node const &
get<0>(YAML::const_iterator::value_type const &v)
{
  return v.first;
}
template <>
inline YAML::Node const &
get<1>(YAML::const_iterator::value_type const &v)
{
  return v.second;
}

template <size_t IDX> YAML::Node get(YAML::iterator::value_type &v);
template <>
inline YAML::Node
get<0>(YAML::iterator::value_type &v)
{
  return v.first;
}
template <>
inline YAML::Node
get<1>(YAML::iterator::value_type &v)
{
  return v.second;
}

// Providing formatting for the node mark - this prints out just the line.
namespace swoc
{
inline BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &, YAML::Mark const &mark)
{
  return w.print("Line {}", mark.line);
}

} // namespace swoc

/// Merge key value for YAML map merging.
static const std::string YAML_MERGE_KEY{"<<"};
/** Perform YAML merging on the tree starting at @a root.
 *
 * @param root Root node.
 * @return The updated @a root node.
 *
 * This does a resursive descent and updates all nodes that are descendants of @a root.
 * The merge keys are removed and replaced with the promoted keys and values.
 *
 * See https://yaml.org/type/merge.html
 */
YAML::Node yaml_merge(YAML::Node root);

swoc::Rv<YAML::Node> yaml_load(swoc::file::path const &path);

namespace YAML
{
#if !defined(YAML_H_62B23520_7C8E_11DE_8A39_0800200C9A66)
// Need these to pass views in to node indexing.
template <> struct convert<std::string_view> {
  static Node
  encode(std::string_view const &sv)
  {
    return Node() = std::string(sv.data(), sv.size());
  }
  static bool
  decode(const Node &node, std::string_view &sv)
  {
    if (!node.IsScalar()) {
      return false;
    }
    sv = std::string_view{node.Scalar()};
    return true;
  }
};
#endif

template <> struct convert<swoc::TextView> {
  static Node
  encode(swoc::TextView const &tv)
  {
    return Node() = std::string(tv.data(), tv.size());
  }
  static bool
  decode(const Node &node, swoc::TextView &tv)
  {
    if (!node.IsScalar()) {
      return false;
    }
    tv.assign(node.Scalar());
    return true;
  }
};

} // namespace YAML
