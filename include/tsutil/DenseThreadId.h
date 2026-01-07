/** @file

  A replacement for std::shared_mutex with guarantees against writer starvation.
  Cache contention between CPU cores is avoided except when a write lock is taken.
  Assumes no thread will exit while holding mutex.

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

#include "tsutil/Assert.h"

#include <array>
#include <cstddef>
#include <mutex>

// Provide an alternate thread id, suitable for use as an array index.
//
class DenseThreadId
{
public:
  static constexpr std::size_t
  num_possible_values()
  {
    return _num_possible_values;
  }

  static std::size_t
  self()
  {
    return _id.val;
  }

private:
  static constexpr std::size_t                                _num_possible_values{256};
  inline static std::mutex                                    _mtx;
  inline static std::array<std::size_t, _num_possible_values> _id_stack;
  inline static std::size_t                                   _stack_top_idx;

  static void
  _init()
  {
    _stack_top_idx = 0;
    for (std::size_t i{0}; i < _num_possible_values; ++i) {
      _id_stack[i] = i + 1;
    }
  }

  struct _Id {
    _Id()
    {
      std::unique_lock<std::mutex> ul{_mtx};

      if (!_inited) {
        _init();
        _inited = true;
      }
      if (_num_possible_values == _stack_top_idx) {
        fatal_error("DenseThreadId:  number of threads exceeded maximum {}", unsigned(_num_possible_values));
      }
      val            = _stack_top_idx;
      _stack_top_idx = _id_stack[_stack_top_idx];
    }

    ~_Id()
    {
      std::unique_lock<std::mutex> ul{_mtx};

      _id_stack[val] = _stack_top_idx;
      _stack_top_idx = val;
    }

    std::size_t val;
  };

  inline static thread_local _Id _id;
  inline static bool             _inited{false};
};
