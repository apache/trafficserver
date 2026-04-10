/** @file
 *
 * Shared context map for jax_fingerprint plugin instances.
 *
 * When multiple jax_fingerprint.so instances are loaded (e.g., one per
 * fingerprinting method), they share a single user arg slot. This map
 * stores the JAxContext for each method, keyed by method name.
 *
 * @section license License
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "config.h"
#include "context.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <version>

/**
 * @brief Container holding JAxContext instances for multiple methods.
 *
 * ATS has a limited number of user arg slots (~4 per type). When loading
 * many jax_fingerprint instances, we share a single slot and store all
 * contexts in this map, keyed by method name.
 */
class ContextMap
{
public:
  ~ContextMap()
  {
    for (auto &pair : m_contexts) {
      delete pair.second;
    }
  }

  /**
   * @brief Store a context for a method.
   * @param[in] method_name The method name (e.g., "JA3", "JA4").
   * @param[in] ctx The context to store. Ownership is transferred.
   */
  void
  set(std::string_view method_name, JAxContext *ctx)
  {
    auto it = find_context(method_name);
    if (it != m_contexts.end()) {
      delete it->second;
      it->second = ctx;
    } else {
      m_contexts.emplace(std::string{method_name}, ctx);
    }
  }

  /**
   * @brief Retrieve a context for a method.
   * @param[in] method_name The method name.
   * @return The context, or nullptr if not found.
   */
  JAxContext *
  get(std::string_view method_name)
  {
    auto it = find_context(method_name);
    return it != m_contexts.end() ? it->second : nullptr;
  }

  /**
   * @brief Remove a context for a method.
   * @param[in] method_name The method name.
   */
  void
  remove(std::string_view method_name)
  {
    auto it = find_context(method_name);
    if (it != m_contexts.end()) {
      delete it->second;
      m_contexts.erase(it);
    }
  }

  /**
   * @brief Check if the map is empty.
   * @return True if no contexts are stored.
   */
  bool
  empty() const
  {
    return m_contexts.empty();
  }

private:
  using ContextStorage = std::unordered_map<std::string, JAxContext *, StringHash, std::equal_to<>>;

  /** Find context by method name with C++20 generic lookup fallback.
   *
   * C++20 generic unordered lookup allows finding with std::string_view in a
   * std::unordered_map<std::string, ...> without creating a temporary string.
   * For standard libraries without this feature, we fall back to constructing
   * a std::string for the lookup.
   *
   * @param[in] method_name The method name to look up.
   * @return Iterator to the found element, or end() if not found.
   */
  ContextStorage::iterator
  find_context(std::string_view method_name)
  {
#ifdef __cpp_lib_generic_unordered_lookup
    return m_contexts.find(method_name);
#else
    return m_contexts.find(std::string{method_name});
#endif
  }

  /** const_iterator @overload */
  ContextStorage::const_iterator
  find_context(std::string_view method_name) const
  {
#ifdef __cpp_lib_generic_unordered_lookup
    return m_contexts.find(method_name);
#else
    return m_contexts.find(std::string{method_name});
#endif
  }

  ContextStorage m_contexts;
};
