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

#include <new>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <vector>
#include <cstdint>

#include <tscpp/util/Strerror.h>

#if __has_include(<tscore/ink_assert.h>)
// Included in core.
#include <tscore/ink_assert.h>
#define L_Assert ink_assert
#include <tscore/Diags.h>
#define L_Fatal Fatal
#else
// Should be plugin code.
#include <ts/ts.h>
#define L_Assert TSAssert
#define L_Fatal TSFatal
#endif

#ifdef X
#error "X preprocessor symbol defined"
#endif

#if !defined(__OPTIMIZE__)

#define X(P) P

#else

#define X(P)

#endif

namespace ts
{
#if defined(_cpp_lib_hardware_interference_size)
std::size_t const CACHE_LINE_SIZE_LCM{std::hardware_destructive_interference_size};
#else
// Least common multiple of cache line size of architectures ATS will run on.
//
std::size_t const CACHE_LINE_SIZE_LCM{128};
#endif

template <typename T> class CacheLineRounded
{
public:
  template <typename... Arg_type> explicit CacheLineRounded(Arg_type &&... arg) : _v{std::forward<Arg_type>(arg)...} {}

  operator T &() { return _v; }
  T &
  operator()()
  {
    return _v;
  }
  operator T const &() const { return _v; }
  T const &
  operator()() const
  {
    return _v;
  }

private:
  static std::size_t const _CLS{CACHE_LINE_SIZE_LCM};
  alignas(_CLS) T _v;
  char _pad[sizeof(T) + ((sizeof(T) % _CLS) ? (_CLS - (sizeof(T) % _CLS)) : 0)];
};

template <typename T> class alignas(CACHE_LINE_SIZE_LCM) CacheLineAligned : public CacheLineRounded<T>
{
public:
  template <typename... Arg_type>
  explicit CacheLineAligned(Arg_type &&... arg) : CacheLineRounded<T>{std::forward<Arg_type>(arg)...}
  {
  }
};

template <typename T> class CacheAlignedDynArrAlloc
{
public:
  CacheAlignedDynArrAlloc(std::size_t n_elems)
  {
    L_Assert(n_elems > 0);

    std::size_t const CLS{CACHE_LINE_SIZE_LCM};

    _mem = new char[(sizeof(CacheLineRounded<T>) * n_elems) + CLS];

    auto addr = reinterpret_cast<std::uintptr_t>(_mem);
    _offset   = (addr % CLS) ? (CLS - (addr % CLS)) : 0;

    _mem += _offset;

    _n_elems = n_elems;

    auto elem = reinterpret_cast<CacheLineRounded<T> *>(_mem);
    for (std::size_t i{0}; i < n_elems; ++i) {
      new (elem) T();
      ++elem;
    }
  }

  ~CacheAlignedDynArrAlloc()
  {
    auto elem = reinterpret_cast<CacheLineRounded<T> *>(_mem);
    for (std::size_t i{0}; i < _n_elems; ++i) {
      (*elem)().~T();
      ++elem;
    }
    delete[](_mem - _offset);
  }

  T &
  operator[](std::size_t idx)
  {
    L_Assert(idx < _n_elems);

    return *reinterpret_cast<T *>(_mem + idx * sizeof(CacheLineRounded<T>));
  }

  T const &
  operator[](std::size_t idx) const
  {
    L_Assert(idx < _n_elems);

    return *reinterpret_cast<T const *>(_mem + idx * sizeof(CacheLineRounded<T>));
  }

  std::size_t
  size() const
  {
    return _n_elems;
  }

  // TODO: add copy and move.
  //
  CacheAlignedDynArrAlloc(CacheAlignedDynArrAlloc const &) = delete;
  CacheAlignedDynArrAlloc &operator=(CacheAlignedDynArrAlloc const &) = delete;

private:
  std::size_t _offset;
  char *_mem;
  std::size_t _n_elems;
};

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
        L_Fatal("DenseThreadId:  number of threads exceeded maximum (%u)", unsigned(_id_stack.size()));
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

// Mutex which can be locked exclusively or shared.  Non-recursive.
//
class scalable_shared_mutex
{
public:
  scalable_shared_mutex()
  {
    for (auto i{_reading_flag.size()}; i;) {
      _reading_flag[--i].store(false, std::memory_order_relaxed);
    }
  }

  // No copying or moving.
  //
  scalable_shared_mutex(scalable_shared_mutex const &) = delete;
  scalable_shared_mutex &operator=(scalable_shared_mutex const &) = delete;

  void
  lock()
  {
    _write_mtx.lock();

    std::unique_lock<std::mutex> ul{_crit.mtx};

    _crit.write_pending().store(true, std::memory_order_seq_cst);

    while (_reading()) {
      _crit.write_ready.wait(ul);
    }

    X(_exclusive = true;)
  }

  // Warning: relying on try_lock() only may result in writer starvation.
  // TODO?  Feasible?  Necessary?
  //
  // bool
  // try_lock()
  // {
  // }

  void
  unlock()
  {
    X(L_Assert(_crit.write_pending());)
    X(L_Assert(_exclusive);)
    {
      std::unique_lock<std::mutex> ul{_crit.mtx};

      _crit.write_pending().store(false, std::memory_order_seq_cst);

      X(_exclusive = false;)
    }
    _crit.read_ready.notify_all();

    _write_mtx.unlock();
  }

  void
  lock_shared()
  {
    auto &rf{_reading_flag[DenseThreadId::self()]};
    if (!_crit.write_pending().load(std::memory_order_seq_cst)) {
      rf.store(true, std::memory_order_seq_cst);
    }
    if (_crit.write_pending().load(std::memory_order_seq_cst)) {
      std::unique_lock<std::mutex> ul{_crit.mtx};
      rf.store(false, std::memory_order_seq_cst);
      while (_crit.write_pending().load(std::memory_order_seq_cst)) {
        if (!_reading()) {
          _crit.write_ready.notify_one();
        }
        _crit.read_ready.wait(ul);
      }
      rf.store(true, std::memory_order_seq_cst);
    }
  }

  // TODO?  Feasible?  Necessary?
  // bool
  // try_lock_shared()
  // {
  // }

  void
  unlock_shared()
  {
    X(L_Assert(!_exclusive);)

    auto &rf{_reading_flag[DenseThreadId::self()]};
    X(L_Assert(rf);)
    rf.store(false, std::memory_order_seq_cst);
    if (!_reading()) {
      if (_crit.write_pending().load(std::memory_order_seq_cst)) {
        // If notify_one() is thread-safe, it probably would be better to simply always notify here, without locking.

        std::unique_lock<std::mutex> ul{_crit.mtx};
        if (_crit.write_pending().load(std::memory_order_seq_cst)) {
          _crit.write_ready.notify_one();
        }
      }
    }
  }

  ~scalable_shared_mutex()
  {
    X(L_Assert(!_crit.write_pending());)
    X(L_Assert(!_reading());)
  }

private:
  CacheAlignedDynArrAlloc<std::atomic<bool>> _reading_flag{DenseThreadId::num_possible_values()};

  bool
  _reading() const
  {
    bool result = false;
    std::atomic_thread_fence(std::memory_order_seq_cst);
    for (auto i{_reading_flag.size()}; i;) {
      if (_reading_flag[--i].load(std::memory_order_relaxed)) {
        result = true;
        break;
      }
    }
    std::atomic_thread_fence(std::memory_order_seq_cst);
    return result;
  }

  // Items written in critical sections.
  //
  struct _Crit {
    std::mutex mtx;

    // This is true while the current writer is either waiting (on write_ready) or in the process of wrting.
    //
    CacheLineAligned<std::atomic<bool>> write_pending{false};

    std::condition_variable write_ready, read_ready;
  };

  _Crit _crit;

  // This ensures there is only one active writer at a time.
  //
  std::mutex _write_mtx;

  X(std::atomic<bool> _exclusive;)
};

} // end namespace ts

#undef X
#undef L_Assert
#undef L_Fatal
