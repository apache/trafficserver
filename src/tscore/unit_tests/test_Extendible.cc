/** @file
  Test file for Extendible
  @section license License
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

#include "tscore/Extendible.h"
#include <iostream>
#include <string>
#include <vector>
#include <ctime>
#include <thread>
#include "tscore/ink_atomic.h"

using namespace std;
using namespace MT;

// Extendible is abstract and must be derived in a CRTP
struct Derived : Extendible<Derived> {
  string m_str;
};

// something to store more complex than an int
struct testField {
  uint8_t arr[5];
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

TEST_CASE("Extendible", "")
{
  typename Derived::BitFieldId bit_a, bit_b, bit_c;
  typename Derived::FieldId<ATOMIC, int> int_a, int_b, int_c;
  Derived *ptr;

  // test cases:
  //[constructor] [operator] [type] [access] [capacity] [modifier] [operation] [compare] [find]
  // I don't use INFOS because this modifies static variables many times, is not thread safe.
  INFO("Extendible()")
  {
    ptr = new Derived();
    REQUIRE(ptr != nullptr);
  }

  INFO("~Extendible")
  {
    //
    delete ptr;
  }

  INFO("Schema Reset")
  {
    ptr = new Derived();
    REQUIRE(Derived::schema.reset() == false);
    delete ptr;
    REQUIRE(Derived::schema.reset() == true);
  }

  INFO("shared_ptr")
  {
    shared_ptr<Derived> sptr(new Derived());
    REQUIRE(sptr);
  }

  INFO("add a bit field")
  {
    //
    REQUIRE(Derived::schema.addField(bit_a, "bit_a"));
  }

  INFO("test bit field")
  {
    shared_ptr<Derived> sptr(new Derived());
    auto &ref = *sptr;
    ref.writeBit(bit_a, 1);
    CHECK(ref[bit_a] == 1);
  }

  INFO("test bit packing")
  {
    REQUIRE(Derived::schema.reset() == true);
    CHECK(Derived::schema.size() == sizeof(std::string));

    REQUIRE(Derived::schema.addField(bit_a, "bit_a"));
    CHECK(Derived::schema.size() == sizeof(std::string) + 1);
    REQUIRE(Derived::schema.addField(bit_b, "bit_b"));
    CHECK(Derived::schema.size() == sizeof(std::string) + 1);
    REQUIRE(Derived::schema.addField(bit_c, "bit_c"));
    CHECK(Derived::schema.size() == sizeof(std::string) + 1);

    shared_ptr<Derived> sptr(new Derived());
    Derived &ref = *sptr;
    ref.writeBit(bit_a, true);
    ref.writeBit(bit_b, false);
    ref.writeBit(bit_c, true);
    CHECK(ref[bit_a] == true);
    CHECK(ref[bit_b] == false);
    CHECK(ref[bit_c] == true);
  }

  INFO("store int field")
  {
    REQUIRE(Derived::schema.addField(int_a, "int_a"));
    REQUIRE(Derived::schema.addField(int_b, "int_b"));
    REQUIRE(Derived::schema.size() == sizeof(std::string) + 1 + sizeof(std::atomic_int) * 2);

    shared_ptr<Derived> sptr(new Derived());
    Derived &ref = *sptr;
    CHECK(ref.get(int_a) == 0);
    CHECK(ref.get(int_b) == 0);
    ++ref.get(int_a);
    ref.get(int_b) = 42;
    ref.m_str      = "Hello";
    CHECK(ref.get(int_a) == 1);
    CHECK(ref.get(int_b) == 42);
    CHECK(ref.m_str == "Hello");
  }

  INFO("C API add int field")
  {
    FieldId_C cf_a = Derived::schema.addField_C("cf_a", 4, nullptr, nullptr);
    CHECK(Derived::schema.size() == sizeof(std::string) + 1 + sizeof(std::atomic_int) * 2 + 4);
    CHECK(Derived::schema.find_C("cf_a") == cf_a);
  }

  INFO("C API alloc instance")
  {
    shared_ptr<Derived> sptr(new Derived());
    CHECK(sptr.get() != nullptr);
  }

  INFO("C API test int field")
  {
    shared_ptr<Derived> sptr(new Derived());
    Derived &ref   = *sptr;
    FieldId_C cf_a = Derived::schema.find_C("cf_a");
    uint8_t *data8 = (uint8_t *)ref.get(cf_a);
    CHECK(data8[0] == 0);
    ink_atomic_increment(data8, 1);
    *(data8 + 1) = 5;
    *(data8 + 2) = 7;

    ref.m_str = "Hello";

    uint32_t *data32 = (uint32_t *)ref.get(cf_a);
    CHECK(*data32 == 0x00070501);
    CHECK(ref.m_str == "Hello");
  }

  Derived::FieldId<ACIDPTR, testField> tf_a;
  INFO("ACIDPTR add field")
  {
    REQUIRE(Derived::schema.addField(tf_a, "tf_a"));
    CHECK(Derived::schema.size() == sizeof(std::string) + 1 + sizeof(std::atomic_int) * 2 + 4 + sizeof(std::shared_ptr<testField>));
    REQUIRE(Derived::schema.find<ACIDPTR, testField>("tf_a").isValid());
  }

  INFO("ACIDPTR test")
  {
    shared_ptr<Derived> sptr(new Derived());
    Derived &ref = *sptr;
    // ref.m_str    = "Hello";
    auto tf_a = Derived::schema.find<ACIDPTR, testField>("tf_a");
    {
      std::shared_ptr<const testField> tf_a_sptr = ref.get(tf_a);
      const testField &dv                        = *tf_a_sptr;
      CHECK(dv.arr[0] == 1);
      CHECK(dv.arr[1] == 2);
      CHECK(dv.arr[2] == 4);
      CHECK(dv.arr[3] == 8);
      CHECK(dv.arr[4] == 16);
    }
    CHECK(testField::alive == 1);
  }

  INFO("ACIDPTR destroyed")
  {
    //
    CHECK(testField::alive == 0);
  }

  INFO("AcidPtr AcidCommitPtr malloc ptr int");
  {
    void *mem            = malloc(sizeof(AcidPtr<int>));
    AcidPtr<int> &reader = *(new (mem) AcidPtr<int>);
    {
      auto writer = reader.startCommit();
      CHECK(*writer == 0);
      *writer = 1;
      CHECK(*writer == 1);
      CHECK(*reader.getPtr().get() == 0);
      // end of scope writer, commit to reader
    }
    CHECK(*reader.getPtr().get() == 1);
    reader.~AcidPtr<int>();
    free(mem);
  }
  INFO("AcidPtr AcidCommitPtr casting");
  {
    void *mem                  = malloc(sizeof(AcidPtr<testField>));
    AcidPtr<testField> &reader = *(new (mem) AcidPtr<testField>);
    {
      auto writer = reader.startCommit();
      CHECK(writer->arr[0] == 1);
      CHECK(reader.getPtr()->arr[0] == 1);
      writer->arr[0] = 99;
      CHECK(writer->arr[0] == 99);
      CHECK(reader.getPtr()->arr[0] == 1);
    }
    CHECK(reader.getPtr()->arr[0] == 99);
    reader.~AcidPtr<testField>();
    free(mem);
  }
  INFO("ACIDPTR block-free reader")
  {
    auto tf_a = Derived::schema.find<ACIDPTR, testField>("tf_a");
    REQUIRE(tf_a.isValid());
    Derived &d = *(new Derived());
    CHECK(d.get(tf_a)->arr[0] == 1);
    { // write 0
      AcidCommitPtr<testField> w = d.writeAcidPtr(tf_a);
      REQUIRE(w != nullptr);
      REQUIRE(w.get() != nullptr);
      w->arr[0] = 0;
    }
    // read 0
    CHECK(d.get(tf_a)->arr[0] == 0);
    // write 1 and read 0
    {
      AcidCommitPtr<testField> tf_a_wtr = d.writeAcidPtr(tf_a);
      tf_a_wtr->arr[0]                  = 1;
      CHECK(d.get(tf_a)->arr[0] == 0);
      // [end of scope] write is committed
    }
    // read 1
    CHECK(d.get(tf_a)->arr[0] == 1);
    delete &d;
  }

  INFO("STATIC")
  {
    typename Derived::FieldId<STATIC, int> tf_d;
    Derived::schema.addField(tf_d, "tf_d");
    REQUIRE(tf_d.isValid());
    Derived &d         = *(new Derived());
    d.init(tf_d)       = 5;
    areStaticsFrozen() = true;

    CHECK(d.get(tf_d) == 5);

    // this asserts when areStaticsFrozen() = true
    // CHECK(d.init(tf_d) == 5);
    delete &d;
  }

  INFO("DIRECT")
  {
    typename Derived::FieldId<DIRECT, int> tf_e;
    Derived::schema.addField(tf_e, "tf_e");
    REQUIRE(tf_e.isValid());
    Derived &d  = *(new Derived());
    d.get(tf_e) = 5;

    CHECK(d.get(tf_e) == 5);

    // this asserts when areStaticsFrozen() = true
    // CHECK(d.init(tf_e) == 5);
    delete &d;
  }

  INFO("Extendible Test Complete")
}
