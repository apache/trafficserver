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

#include "context.h"

#include "ts/ts.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

#ifndef JAX_FINGERPRINT_MAX_METHODS
#define JAX_FINGERPRINT_MAX_METHODS 8
#endif

/**
 * @brief Container holding JAxContext instances for multiple methods.
 *
 * ATS has a limited number of user arg slots (~4 per type). When loading
 * many jax_fingerprint instances, we share a single slot and store all
 * contexts in this inline fixed-size table, keyed by method name. The
 * table size is set at build time via JAX_FINGERPRINT_MAX_METHODS.
 *
 * Lookup is a linear scan over std::string_view keys (Method::name points
 * to a string literal with static storage duration, so storing the view
 * is safe).
 */
class ContextMap
{
public:
  static constexpr std::size_t MAX_METHODS = JAX_FINGERPRINT_MAX_METHODS;
  static_assert(MAX_METHODS >= 1, "Must accommodate at least one fingerprinting method");

  ~ContextMap()
  {
    for (std::uint8_t i = 0; i < _size; ++i) {
      delete _slots[i].second;
    }
  }

  /**
   * @brief Store a context for a method.
   * @param[in] method_name The method name (e.g., "JA3", "JA4"). Must reference
   *   a string with lifetime >= the ContextMap (typically a string literal).
   * @param[in] ctx The context to store. Ownership is transferred.
   */
  void
  set(std::string_view method_name, JAxContext *ctx)
  {
    for (std::uint8_t i = 0; i < _size; ++i) {
      if (_slots[i].first == method_name) {
        delete _slots[i].second;
        _slots[i].second = ctx;
        return;
      }
    }
    TSReleaseAssert(_size < MAX_METHODS);
    _slots[_size++] = {method_name, ctx};
  }

  /**
   * @brief Retrieve a context for a method.
   * @param[in] method_name The method name.
   * @return The context, or nullptr if not found.
   */
  JAxContext *
  get(std::string_view method_name) const
  {
    for (std::uint8_t i = 0; i < _size; ++i) {
      if (_slots[i].first == method_name) {
        return _slots[i].second;
      }
    }
    return nullptr;
  }

  /**
   * @brief Remove a context for a method.
   * @param[in] method_name The method name.
   */
  void
  remove(std::string_view method_name)
  {
    for (std::uint8_t i = 0; i < _size; ++i) {
      if (_slots[i].first == method_name) {
        delete _slots[i].second;
        --_size;
        if (i != _size) {
          _slots[i] = _slots[_size];
        }
        _slots[_size] = {};
        return;
      }
    }
  }

  /**
   * @brief Check if the map is empty.
   * @return True if no contexts are stored.
   */
  bool
  empty() const
  {
    return _size == 0;
  }

private:
  std::array<std::pair<std::string_view, JAxContext *>, MAX_METHODS> _slots{};
  std::uint8_t                                                       _size{0};
};
