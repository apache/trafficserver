/** @file
  Test file for Extendible
  @INFO license License
  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at
      http://www.apache.org/licenses/LICENSE-2.0
  Unless REQUIRE by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

#include "catch.hpp"

#include <iostream>
#include <string>
#include <array>
#include <ctime>
#include <thread>

#include "tscore/ink_atomic.h"
#include "tscore/Extendible.h"

using namespace std;
using namespace ext;

//////////////////////////////////////////////////////
// AtomicBit Tests
TEST_CASE("AtomicBit Atomic test")
{
  // test the atomicity and isolation of operations
  uint32_t bit_storage = 0;

  auto job_set   = [&bit_storage](int idx) { AtomicBit{(uint8_t *)&bit_storage + (idx / 8), (uint8_t)(1 << (idx % 8))} = 1; };
  auto job_clear = [&bit_storage](int idx) { AtomicBit{(uint8_t *)&bit_storage + (idx / 8), (uint8_t)(1 << (idx % 8))} = 0; };

  std::thread jobs[32];

  // set all bits in parallel
  for (int i = 0; i < 32; i++) {
    jobs[i] = std::thread(job_set, i);
  }
  for (int i = 0; i < 32; i++) {
    jobs[i].join();
  }
  REQUIRE(bit_storage == 0xffffffff);

  // clear all bits in parallel
  for (int i = 0; i < 32; i++) {
    jobs[i] = std::thread(job_clear, i);
  }
  for (int i = 0; i < 32; i++) {
    jobs[i].join();
  }
  REQUIRE(bit_storage == 0);
}

//////////////////////////////////////////////////////
// Extendible Inheritance Tests

struct A : public Extendible<A> {
  using self_type = A;
  DEF_EXT_NEW_DEL(self_type);
  uint16_t a = {1};
};

ext::FieldId<A, std::atomic<uint16_t>> ext_a_1;

class B : public A
{
public:
  using super_type = A;
  using self_type  = B;
  DEF_EXT_NEW_DEL(self_type);
  uint16_t b = {2};
};

class C : public B, public Extendible<C>
{
public:
  using super_type = B;
  using self_type  = C;
  DEF_EXT_NEW_DEL(self_type);
  uint16_t c = {3};

  // operator[]
  template <typename F>
  decltype(auto)
  operator[](F field) const
  {
    return ext::get(*this, field);
  }
  template <typename F>
  decltype(auto)
  operator[](F field)
  {
    return ext::set(*this, field);
  }
};

ext::FieldId<C, std::atomic<uint16_t>> ext_c_1;

uintptr_t
memDelta(void *p, void *q)
{
  return uintptr_t(q) - uintptr_t(p);
}
A *a_ptr = nullptr;
TEST_CASE("Create A", "")
{
  ext::details::areFieldsFinalized() = true;
  a_ptr                              = ext::create<A>();
  CHECK(Extendible<A>::schema.no_instances() == false);
}
TEST_CASE("Delete A", "")
{
  delete a_ptr;
  CHECK(Extendible<A>::schema.no_instances());
}
TEST_CASE("Create B", "")
{
  a_ptr = ext::create<B>();
  CHECK(Extendible<A>::schema.no_instances() == false);
}
TEST_CASE("Delete B", "")
{
  delete a_ptr;
  CHECK(Extendible<A>::schema.no_instances());
}
TEST_CASE("Create C", "")
{
  a_ptr = ext::create<C>();
  CHECK(Extendible<A>::schema.no_instances() == false);
  CHECK(Extendible<C>::schema.no_instances() == false);
}
TEST_CASE("Delete C", "")
{
  delete static_cast<C *>(a_ptr);
  CHECK(Extendible<A>::schema.no_instances());
  CHECK(Extendible<C>::schema.no_instances());
  CHECK(Extendible<A>::schema.cnt_constructed == 3);
  CHECK(Extendible<A>::schema.cnt_fld_constructed == 3);
  CHECK(Extendible<A>::schema.cnt_destructed == 3);
  CHECK(Extendible<C>::schema.cnt_constructed == 1);
  CHECK(Extendible<C>::schema.cnt_fld_constructed == 1);
  CHECK(Extendible<C>::schema.cnt_destructed == 1);
}
TEST_CASE("Extendible Memory Allocations", "")
{
  ext::details::areFieldsFinalized() = false;
  fieldAdd(ext_a_1, "ext_a_1");
  fieldAdd(ext_c_1, "ext_c_1");
  ext::details::areFieldsFinalized() = true;

  size_t w = sizeof(uint16_t);
  CHECK(ext::sizeOf<A>() == w * 3);
  CHECK(ext::sizeOf<B>() == w * 4);
  CHECK(ext::sizeOf<C>() == w * 7);

  C &x = *(ext::create<C>());
  //    0   1   2   3   4   5   6
  //[ EA*,  a,  b,EC*,  c, EA, EC]
  //
  uint16_t *mem = (uint16_t *)&x;
  CHECK(memDelta(&x, &x.a) == w * 1);
  CHECK(memDelta(&x, &x.b) == w * 2);
  CHECK(memDelta(&x, &x.c) == w * 4);
  CHECK(mem[0] == w * (5 - 0));
  CHECK(mem[1] == 1);
  CHECK(mem[2] == 2);
  CHECK(mem[3] == w * (6 - 3));
  CHECK(mem[4] == 3);
  CHECK(mem[5] == 0);
  CHECK(mem[6] == 0);

  std::string format = "\n                            1A | EXT  |     2b |##________##__"
                       "\n                            1A | BASE |     2b |__##__________"
                       "\n                            1B | BASE |     2b |____##________"
                       "\n                            1C | EXT  |     2b |______##____##"
                       "\n                            1C | BASE |     2b |________##____";
  CHECK(ext::viewFormat(x) == format);

  printf("\n");
  delete &x;
}

TEST_CASE("Extendible Pointer Math", "")
{
  C &x = *(ext::create<C>());

  CHECK(x.a == 1);
  CHECK(x.b == 2);
  CHECK(x.c == 3);

  ext::set(x, ext_a_1) = 4;
  CHECK(ext::get(x, ext_a_1) == 4);
  x[ext_c_1] = 5;
  CHECK(ext::get(x, ext_c_1) == 5);

  CHECK(x.a == 1);
  CHECK(x.b == 2);
  CHECK(x.c == 3);
  CHECK(ext::get(x, ext_a_1) == 4);
  CHECK(ext::get(x, ext_c_1) == 5);

  std::string format = "\n                            1A | EXT  |     2b |##________##__"
                       "\n                            1A | BASE |     2b |__##__________"
                       "\n                            1B | BASE |     2b |____##________"
                       "\n                            1C | EXT  |     2b |______##____##"
                       "\n                            1C | BASE |     2b |________##____";
  CHECK(ext::viewFormat(x) == format);

  ext::FieldId<A, bool> a_bit;
  ext::FieldId<A, int> a_int;
  static_assert(std::is_same<decltype(ext::get(x, a_bit)), bool>::value);
  static_assert(std::is_same<decltype(ext::get(x, a_int)), int const &>::value);
  delete &x;
}

// Extendible is abstract and must be derived in a CRTP
struct Derived : Extendible<Derived> {
  using self_type = Derived;
  DEF_EXT_NEW_DEL(self_type);
  string m_str;

  // operator[] for shorthand
  template <typename F>
  decltype(auto)
  operator[](F field) const
  {
    return ext::get(*this, field);
  }
  template <typename F>
  decltype(auto)
  operator[](F field)
  {
    return ext::set(*this, field);
  }

  static const string
  testFormat()
  {
    const size_t intenal_size = sizeof(Derived) - sizeof(ext::Extendible<Derived>::short_ptr_t);
    std::stringstream format;
    format << "\n                      7Derived | EXT  |     1b |##" << string(intenal_size, '_') << "#"
           << "\n                      7Derived | BASE | " << setw(5) << intenal_size << "b |__" << string(intenal_size, '#')
           << "_";
    return format.str();
  }
};

///////////////////////////////////////
// C API for Derived
//
void *
DerivedExtalloc()
{
  return ext::create<Derived>();
}
void
DerivedExtFree(void *ptr)
{
  delete (Derived *)ptr;
}

ExtFieldContext
DerivedExtfieldAdd(char const *field_name, int size, void (*construct_fn)(void *), void (*destruct_fn)(void *))
{
  // hack to avoid having to repeat this in testing.
  ext::details::areFieldsFinalized() = false;
  auto r                             = fieldAdd<Derived>(field_name, size, construct_fn, destruct_fn);
  ext::details::areFieldsFinalized() = true;
  return r;
}
ExtFieldContext
DerivedExtfieldFind(char const *field_name)
{
  return fieldFind<Derived>(field_name);
}

///////////////////////////////////////

// something to store more complex than an int
struct testField {
  std::array<uint8_t, 5> arr;
  static int alive;
  testField()
  {
    uint8_t x = 1;
    for (uint8_t &a : arr) {
      a = x;
      x *= 2;
    }
    alive++;
  }
  ~testField()
  {
    for (uint8_t &a : arr) {
      a = 0;
    }
    alive--;
  }
};
int testField::alive = 0;

namespace ext
{
template <>
void
serializeField(ostream &os, testField const &t)
{
  serializeField(os, t.arr);
}
} // namespace ext

TEST_CASE("Extendible", "")
{
  printf("\nsizeof(string) = %lu", sizeof(std::string));
  printf("\nsizeof(Derived) = %lu", sizeof(Derived));

  ext::FieldId<Derived, bool> bit_a, bit_b, bit_c;
  ext::FieldId<Derived, std::atomic<int>> int_a, int_b, int_c;
  Derived *ptr;

  // test cases:
  //[constructor] [operator] [type] [access] [capacity] [modifier] [operation] [compare] [find]
  // I don't use SECTIONS because this modifies static variables many times, is not thread safe.
  INFO("Extendible()")
  {
    ptr = ext::create<Derived>();
    REQUIRE(ptr != nullptr);
  }

  INFO("~Extendible")
  {
    //
    delete ptr;
  }

  INFO("Schema Reset")
  {
    ptr = ext::create<Derived>();
    REQUIRE(Derived::schema.no_instances() == false);
    REQUIRE(Derived::schema.reset() == false);
    delete ptr;
    REQUIRE(Derived::schema.no_instances() == true);
    ext::details::areFieldsFinalized() = false;
    REQUIRE(Derived::schema.reset() == true);
    ext::details::areFieldsFinalized() = true;
  }

  INFO("shared_ptr")
  {
    shared_ptr<Derived> sptr(ext::create<Derived>());
    REQUIRE(Derived::schema.no_instances() == false);
    REQUIRE(sptr);
  }
  REQUIRE(Derived::schema.no_instances() == true);

  INFO("add a bit field")
  {
    //
    ext::details::areFieldsFinalized() = false;
    REQUIRE(fieldAdd(bit_a, "bit_a"));
    ext::details::areFieldsFinalized() = true;
  }

  INFO("Extendible delete ptr");
  {
    for (int i = 0; i < 10; i++) {
      ptr = ext::create<Derived>();
      REQUIRE(ptr != nullptr);
      INFO(__LINE__);
      REQUIRE(Derived::schema.no_instances() == false);
      delete ptr;
      INFO(__LINE__);
      ptr = nullptr;
      REQUIRE(Derived::schema.no_instances() == true);
    }
  }

  INFO("test bit field");
  {
    shared_ptr<Derived> sptr{ext::create<Derived>()};
    Derived &ref = *sptr;

    CHECK(ext::viewFormat(ref) == Derived::testFormat());

    AtomicBit bitref = ext::set(ref, bit_a);
    bitref           = 1;
    CHECK(bitref == true);
    bitref = true;
    CHECK(bitref == true);
    CHECK(ext::set(ref, bit_a) == true);
    CHECK(ext::get(ref, bit_a) == true);
  }

  INFO("test bit packing")
  {
    struct size_test {
      string s;
      uint16_t i;
    };

    REQUIRE(Derived::schema.reset() == true);
    CHECK(sizeof(Extendible<Derived>) == sizeof(uint16_t));
    CHECK(sizeof(size_test) == ROUNDUP(sizeof(std::string) + sizeof(uint16_t), alignof(std::string)));
    CHECK(sizeof(Derived) == sizeof(size_test));
    CHECK(ext::sizeOf<Derived>() == sizeof(Derived));

    ext::details::areFieldsFinalized() = false;
    REQUIRE(fieldAdd(bit_a, "bit_a"));
    size_t expected_size = sizeof(Derived) + 1;
    CHECK(ext::sizeOf<Derived>() == expected_size);
    REQUIRE(fieldAdd(bit_b, "bit_b"));
    CHECK(ext::sizeOf<Derived>() == expected_size);
    REQUIRE(fieldAdd(bit_c, "bit_c"));
    CHECK(ext::sizeOf<Derived>() == expected_size);
    ext::details::areFieldsFinalized() = true;

    shared_ptr<Derived> sptr(ext::create<Derived>());
    Derived &ref = *sptr;
    CHECK(ext::viewFormat(ref) == Derived::testFormat());
    using Catch::Matchers::Contains;
    REQUIRE_THAT(ext::toString(ref), Contains("bit_a: 0"));
    REQUIRE_THAT(ext::toString(ref), Contains("bit_b: 0"));
    REQUIRE_THAT(ext::toString(ref), Contains("bit_c: 0"));

    ext::set(ref, bit_a) = 1;
    ext::set(ref, bit_b) = 0;
    ext::set(ref, bit_c) = 1;
    CHECK(ext::get(ref, bit_a) == true);
    CHECK(ext::get(ref, bit_b) == false);
    CHECK(ext::get(ref, bit_c) == true);
    REQUIRE_THAT(ext::toString(ref), Contains("bit_a: 1"));
    REQUIRE_THAT(ext::toString(ref), Contains("bit_b: 0"));
    REQUIRE_THAT(ext::toString(ref), Contains("bit_c: 1"));
  }

  INFO("store int field")
  {
    ext::details::areFieldsFinalized() = false;
    REQUIRE(fieldAdd(int_a, "int_a"));
    REQUIRE(fieldAdd(int_b, "int_b"));
    ext::details::areFieldsFinalized() = true;

    size_t expected_size = sizeof(Derived) + 1 + sizeof(std::atomic_int) * 2;
    CHECK(ext::sizeOf<Derived>() == expected_size);

    shared_ptr<Derived> sptr(ext::create<Derived>());
    Derived &ref = *sptr;
    CHECK(ext::get(ref, int_a) == 0);
    CHECK(ext::get(ref, int_b) == 0);
    ++ext::set(ref, int_a);
    ext::set(ref, int_b) = 42;
    ref.m_str            = "Hello";
    CHECK(ext::get(ref, int_a) == 1);
    CHECK(ext::get(ref, int_b) == 42);
    CHECK(ref.m_str == "Hello");
  }

  INFO("Extendible Test Complete")
}

TEST_CASE("Extendible C API")
{
  ext::details::areFieldsFinalized() = false;
  Derived::schema.reset();
  CHECK(Derived::schema.no_instances() == true);
  ext::details::areFieldsFinalized() = true;

  INFO("C API alloc instance")
  {
    void *d = DerivedExtalloc();

    CHECK(d != nullptr);
    CHECK(Derived::schema.no_instances() == false);

    DerivedExtFree(d);

    CHECK(Derived::schema.no_instances() == true);
  }

  INFO("C API add int field")
  {
    ExtFieldContext cf_a = DerivedExtfieldAdd("cf_a", 4, nullptr, nullptr);

    size_t expected_size = sizeof(Derived) + 4;
    CHECK(ext::sizeOf<Derived>() == expected_size);
    CHECK(DerivedExtfieldFind("cf_a") == cf_a);
  }
  INFO("C API test int field")
  {
    void *d = DerivedExtalloc();
    REQUIRE(d != nullptr);

    ExtFieldContext cf_a = DerivedExtfieldFind("cf_a");
    uint8_t *data8       = (uint8_t *)ExtFieldPtr(d, cf_a);

    CHECK(data8[0] == 0);
    ink_atomic_increment(&data8[0], 1);
    data8[1] = 5;
    data8[2] = 7;

    uint32_t *data32 = (uint32_t *)ExtFieldPtr(d, cf_a);
    CHECK(*data32 == 0x00070501);
    DerivedExtFree(d);
  }
  INFO("Extendible C API Test Complete")
}

//*/
