/** @file

  Provides a "versioned pointer" data type.

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

#include <cstdint>
#include <cstring>
#include <atomic>

#include <tscore/ink_config.h>

namespace ts
{
namespace detail
{
  namespace versioned_ptr
  {
    // This is true if pointers are 64 bits but the two most significant bytes are not used, except for the
    // most significant bit.
    //
    bool const Ptr64bits15unused =
#if defined(__x86_64__) || defined(__ia64__) || defined(__powerpc64__) || defined(__aarch64__) || defined(__mips64)
      true
#else
      false
#endif
      ;

#if TS_HAS_128BIT_CAS

    bool const CAE128 = true;

    using Bits128_type = __int128_t;

#else

    bool const CAE128 = false;

    using Bits128_type = char; // Not used.

#endif

    // std::atomic<__int128_t> seems to be a work-in-progress, so avoid using it.

    template <typename T> struct Atomic_wrap {
      using Type = std::atomic<T>;
    };

    template <> struct Atomic_wrap<Bits128_type> {
      // Note:  The __sync_xyz functions used here are deprecated by the GCC documentation.  However,
      // the "replacements" generate calls to run-time library functions, whereas these are inlined (at
      // lease for x86 64-bit), and therefore seem preferable.
      //
      // There don't seem to be 128-bit atomic load and store instructions for x86-64, so using the
      // 128-bit compare-and-exchange as a work-around.
      //
      class Type
      {
      public:
        explicit Type(Bits128_type v) : _val(v) {}

        Bits128_type
        load()
        {
          __sync_synchronize();

          Bits128_type result = __sync_val_compare_and_swap(&_val, 0, 0);

          __sync_synchronize();

          return result;
        }

        void
        store(Bits128_type new_v)
        {
          __sync_synchronize();

          Bits128_type old_v, curr_v = _val;

          for (;;) {
            old_v = __sync_val_compare_and_swap(&_val, curr_v, new_v);
            if (old_v == curr_v) {
              break;
            }
            curr_v = old_v;
          }

          __sync_synchronize();
        }

        bool
        compare_exchange_strong(Bits128_type &expected, Bits128_type desired)
        {
          bool result = false;

          __sync_synchronize();

          Bits128_type curr = __sync_val_compare_and_swap(&_val, expected, desired);
          if (curr == expected) {
            result = true;

          } else {
            expected = curr;
          }
          __sync_synchronize();

          return result;
        }

        // Never actually weak.
        //
        bool
        compare_exchange_weak(Bits128_type &expected, Bits128_type desired)
        {
          return compare_exchange_strong(expected, desired);
        }

      private:
        alignas(16) Bits128_type _val;
      };
    };

    struct VPS {
      void *ptr;
      unsigned version;
    };

    inline bool
    operator==(VPS const &a, VPS const &b)
    {
      return (a.ptr == b.ptr) && (a.version == b.version);
    }

    // The default versioned pointer type.  Atomically accessed in an instance of the AAT (atomic access type).
    //
    template <typename AAT, bool Version_in_ptr> class Type_
    {
    public:
      using Atomic_access_type = AAT;

      explicit Type_(void *p = nullptr, unsigned v = 0)
      {
        _vp.ptr     = p;
        _vp.version = v;

        const std::size_t Pad = sizeof(_aa) - sizeof(_vp);
        if (Pad) {
          // Make sure pad bytes are consistently zero.
          //
          std::memset(reinterpret_cast<char *>(&_vp) + sizeof(_vp), 0, Pad);
        }
      }

      explicit Type_(Atomic_access_type const &aa) { _aa = aa; }

      void *
      ptr() const
      {
        return _vp.ptr;
      }

      void
      ptr(void *p)
      {
        _vp.ptr = p;
      }

      unsigned
      version() const
      {
        return _vp.version;
      }

      void
      version(unsigned v)
      {
        _vp.version = v;
      }

    private:
      union {
        VPS _vp;
        AAT _aa;
      };

      static_assert(sizeof(_vp) <= sizeof(_aa));
    };

    namespace
    {
      unsigned const Version_bits  = 15;
      unsigned const Version_shift = 48;
      constexpr unsigned
      Mask_version(unsigned v)
      {
        return v & ((1U << (Version_bits + 1)) - 1);
      }
      std::uint64_t const Version_mask = static_cast<std::uint64_t>(Mask_version(~0U)) << Version_shift;

    } // end anonymous namespace

    // In this specialization, the version is kept in 15 unused bits in a 64-bit pointer.
    //
    template <> class Type_<std::uint64_t, true>
    {
    public:
      using Atomic_access_type = std::uint64_t;

      Type_(){};

      explicit Type_(void *p, unsigned v = 0)
      {
        _ptr = reinterpret_cast<std::uint64_t>(p) & ~Version_mask;
        _ptr |= static_cast<uint64_t>(Mask_version(v)) << Version_shift;
      }

      explicit Type_(Atomic_access_type const &ptr) { _ptr = ptr; }

      void *
      ptr() const
      {
        return reinterpret_cast<void *>(_ptr & ~Version_mask);
      }

      void
      ptr(void *p)
      {
        _ptr &= Version_mask;
        _ptr |= reinterpret_cast<std::uint64_t>(p) & ~Version_mask;
      }

      unsigned
      version() const
      {
        return Mask_version(static_cast<unsigned>(_ptr >> Version_shift));
      }

      void
      version(unsigned v)
      {
        _ptr &= ~Version_mask;
        _ptr |= static_cast<std::uint64_t>(Mask_version(v)) << Version_shift;
      }

    private:
      std::uint64_t _ptr;
    };

    enum Impl_type {
      DEFAULT,        // Atomic access may not be lock free.
      VERSION_IN_PTR, // Use bits inside of 64-bit pointer to store version.
      BITS64,         // VPS structure is less than 64 bits, and 64-bit access is lock free.
      BITS128         // VPS structure is less than 128 bits, and 128-bit compare-and-exchange is available.
    };

    constexpr Impl_type
    Impl()
    {
      if (std::atomic<std::uint64_t>::is_always_lock_free && (sizeof(VPS) <= 8)) {
        return BITS64;

      } else if (CAE128 && (sizeof(VPS) <= 16)) {
        return BITS128;

      } else if (std::atomic<std::uint64_t>::is_always_lock_free && Ptr64bits15unused && (sizeof(void *) == 8)) {
        return VERSION_IN_PTR;
      }
      return DEFAULT;
    }

    template <Impl_type> struct Sel {
      using Type = Type_<VPS, false>;
    };

    template <> struct Sel<VERSION_IN_PTR> {
      using Type = Type_<std::uint64_t, true>;
    };

    template <> struct Sel<BITS64> {
      using Type = Type_<std::uint64_t, false>;
    };

    template <> struct Sel<BITS128> {
      using Type = Type_<Bits128_type, false>;
    };

  } // end namespace versioned_ptr
} // end namespace detail

using Versioned_ptr = detail::versioned_ptr::Sel<detail::versioned_ptr::Impl()>::Type;

class Atomic_versioned_ptr
{
public:
  explicit Atomic_versioned_ptr(Versioned_ptr vp = Versioned_ptr{nullptr, 0}) : _val{_to_aa(vp)} {}

  Versioned_ptr
  load()
  {
    return Versioned_ptr(_val.load());
  }

  void
  store(Versioned_ptr vp)
  {
    _val.store(_to_aa(vp));
  }

  bool
  compare_exchange_weak(Versioned_ptr &expected, Versioned_ptr desired)
  {
    return _val.compare_exchange_weak(_to_aa(expected), _to_aa(desired));
  }

  bool
  compare_exchange_strong(Versioned_ptr &expected, Versioned_ptr desired)
  {
    return _val.compare_exchange_strong(_to_aa(expected), _to_aa(desired));
  }

  bool
  compare_exchange_weak(Versioned_ptr &expected, void *desired)
  {
    return compare_exchange_weak(expected, Versioned_ptr(desired, expected.version() + 1));
  }

  bool
  compare_exchange_strong(Versioned_ptr &expected, void *desired)
  {
    return compare_exchange_strong(expected, Versioned_ptr(desired, expected.version() + 1));
  }

private:
  static Versioned_ptr::Atomic_access_type &
  _to_aa(Versioned_ptr &vp)
  {
    return *reinterpret_cast<Versioned_ptr::Atomic_access_type *>(&vp);
  }

  detail::versioned_ptr::Atomic_wrap<Versioned_ptr::Atomic_access_type>::Type _val;
};

} // end namespace ts
