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

#include <cstddef>
#include <mutex>
#include <vector>

// Provide an alternate thread id, suitible for use as an array index.
//
class DenseThreadId
{
public:
  // This can onlhy be called during single-threaded initialization.
  //
  static void
  set_num_possible_values(std::size_t num_possible_values)
  {
    _num_possible_values = num_possible_values;
  }

  static std::size_t
  self()
  {
    return _id.val;
  }

  static std::size_t
  num_possible_values()
  {
    return _num_possible_values;
  }

private:
  inline static std::mutex _mtx;
  inline static std::vector<std::size_t> _id_stack;
  inline static std::size_t _stack_top_idx;
  inline static std::size_t _num_possible_values{256};

  static void
  _init()
  {
    _id_stack.resize(_num_possible_values);

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
      if (_id_stack.size() == _stack_top_idx) {
        fatal_error("DenseThreadId:  number of threads exceeded maximum {}", unsigned(_id_stack.size()));
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
  inline static bool _inited{false};
};
