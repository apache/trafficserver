/** @file
 * YAML utilities.
 *
 * Copyright 2019, Oath Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string>

#include "txn_box/common.h"
#include "txn_box/yaml_util.h"
#include <swoc/bwf_std.h>

using swoc::TextView;
using namespace swoc::literals;
using swoc::Errata;
using swoc::Rv;

/* ------------------------------------------------------------------------------------ */
YAML::Node
yaml_merge(YAML::Node root)
{
  static constexpr auto flatten = [](YAML::Node dst, YAML::Node const &src) -> void {
    if (src.IsMap()) {
      for (auto const &[key, value] : src) {
        // don't need to check for nested merge key, because this function is called only if
        // that's already set in @a dst therefore it won't be copied up from @a src.
        if (!dst[key]) {
          dst[key] = value;
        }
      }
    }
  };

  if (root.IsSequence()) {
    for (auto child : root) {
      yaml_merge(child);
    }
  } else if (root.IsMap()) {
    // Do all nested merges first, so the result is iteration order independent.
    for (auto [key, value] : root) {
      value = yaml_merge(value);
    }
    // If there's a merge key, merge it in.
    if (auto merge_node{root[YAML_MERGE_KEY]}; merge_node) {
      if (merge_node.IsMap()) {
        flatten(root, merge_node);
      } else if (merge_node.IsSequence()) {
        for (auto src : merge_node) {
          flatten(root, src);
        }
      }
      root.remove(YAML_MERGE_KEY);
    }
  }
  return root;
}

Rv<YAML::Node>
yaml_load(swoc::file::path const &path)
{
  //  static_assert(sizeof(YAML::Node) == sizeof(static_cast<Rv<YAML::Node>*>(nullptr)->_r));
  std::error_code ec;
  std::string content = swoc::file::load(path, ec);

  if (ec) {
    return Errata(S_ERROR, R"(Unable to load file "{}" - {}.)", path, ec);
  }

  YAML::Node root;
  try {
    root = YAML::Load(content);
  } catch (std::exception &ex) {
    return Errata(S_ERROR, R"(YAML parsing of "{}" failed - {}.)", path, ex.what());
  }

  yaml_merge(root);
  [[maybe_unused]] auto s1 = sizeof(Rv<YAML::Node>::result_type);
  [[maybe_unused]] auto s2 = sizeof(YAML::Node);
  static_assert(sizeof(Rv<YAML::Node>) > sizeof(YAML::Node));
  return root;
}
