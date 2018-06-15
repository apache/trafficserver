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

#include "ts/Extendible.h"
#include <iostream>
#include <string>
#include <vector>
#include <ctime>
#include <thread>
#include "ts/ink_atomic.h"

using namespace std;
using namespace MT;

// Extendible is abstract and must be derived in a CRTP
struct Derived : Extendible<Derived> {
  using super_type = Extendible<Derived>;
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

TEST_CASE("read_ptr write_ptr")
{
  read_ptr<int> p;
  REQUIRE(p.get() != nullptr);
  REQUIRE(p.get().get() != nullptr);
  {
    write_ptr<int> w(p);
    *w = 40;
  }
  CHECK(*p.get() == 40);
  {
    write_ptr<int> w = p;
    *w += 1;
    CHECK(*p.get() == 40); // new value not commited until end of scope
  }
  CHECK(*p.get() == 41);
  {
    *write_ptr<int>(p) += 1; // leaves scope immediately if not named.
    CHECK(*p.get() == 42);
  }
  CHECK(*p.get() == 42);
}

TEST_CASE("Extendible", "")
{
  typename Derived::super_type::BitFieldId bit_a, bit_b, bit_c;
  typename Derived::super_type::FieldId<ATOMIC, int> int_a, int_b, int_c;
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
    FieldId_c cf_a = Derived::schema.addField_c("cf_a", 4, nullptr, nullptr);
    CHECK(Derived::schema.size() == sizeof(std::string) + 1 + sizeof(std::atomic_int) * 2 + 4);
    CHECK(Derived::schema.find_c("cf_a") == cf_a);
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
    FieldId_c cf_a = Derived::schema.find_c("cf_a");
    uint8_t *data8 = (uint8_t *)ref.get_c(cf_a);
    CHECK(data8[0] == 0);
    ink_atomic_increment(data8, 1);
    *(data8 + 1) = 5;
    *(data8 + 2) = 7;

    ref.m_str = "Hello";

    uint32_t *data32 = (uint32_t *)ref.get_c(cf_a);
    CHECK(*data32 == 0x00070501);
    CHECK(ref.m_str == "Hello");
  }

  Derived::FieldId<COPYSWAP, testField> tf_a;
  INFO("COPYSWAP add field")
  {
    REQUIRE(Derived::schema.addField(tf_a, "tf_a"));
    CHECK(Derived::schema.size() == sizeof(std::string) + 1 + sizeof(std::atomic_int) * 2 + 4 + sizeof(std::shared_ptr<testField>));
    REQUIRE(Derived::FieldId<COPYSWAP, testField>::find("tf_a").isValid());
  }

  INFO("COPYSWAP test")
  {
    shared_ptr<Derived> sptr(new Derived());
    Derived &ref = *sptr;
    // ref.m_str    = "Hello";
    auto tf_a = Derived::FieldId<COPYSWAP, testField>::find("tf_a");
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

  INFO("COPYSWAP destroyed")
  {
    //
    CHECK(testField::alive == 0);
  }

  INFO("read_ptr write_ptr casting");
  {
    void *mem = malloc(sizeof(read_ptr<testField>));
    new (mem) read_ptr<testField>();
    read_ptr<testField> &reader = *static_cast<read_ptr<testField> *>(mem);
    {
      write_ptr<testField> writer = write_ptr<testField>(reader);
      CHECK(writer->arr[0] == 1);
      CHECK(reader.get()->arr[0] == 1);
      writer->arr[0] = 99;
      CHECK(writer->arr[0] == 99);
      CHECK(reader.get()->arr[0] == 1);
    }
    CHECK(reader.get()->arr[0] == 99);
    free(mem);
  }
  INFO("COPYSWAP block-free reader")
  {
    auto tf_a = Derived::FieldId<COPYSWAP, testField>::find("tf_a");
    REQUIRE(tf_a.isValid());
    Derived &d = *(new Derived());
    CHECK(d.get(tf_a)->arr[0] == 1);
    { // write 0
      write_ptr<testField> w = d.writeCopySwap(tf_a);
      REQUIRE(w != nullptr);
      REQUIRE(w.get() != nullptr);
      w->arr[0] = 0;
    }
    // read 0
    CHECK(d.get(tf_a)->arr[0] == 0);
    // write 1 and read 0
    {
      write_ptr<testField> tf_a_wtr = d.writeCopySwap(tf_a);
      tf_a_wtr->arr[0]              = 1;
      CHECK(d.get(tf_a)->arr[0] == 0);
      // [end of scope] write is committed
    }
    // read 1
    CHECK(d.get(tf_a)->arr[0] == 1);
    delete &d;
  }

  INFO("COPYSWAP timing test")
  if (0) {
    const int N = 1000;
    Derived::FieldId<ATOMIC, uint32_t> fld_read_duration;
    Derived::FieldId<ATOMIC, uint32_t> fld_write_duration;
    REQUIRE(Derived::schema.addField(fld_read_duration, "read_duration"));
    REQUIRE(Derived::schema.addField(fld_write_duration, "write_duration"));

    shared_ptr<Derived> sptr(new Derived());
    auto reader_test = [sptr, tf_a, fld_read_duration]() {
      auto start                                       = std::chrono::system_clock::now();
      const std::shared_ptr<const testField> tf_a_sptr = sptr->get(tf_a);
      auto end                                         = std::chrono::system_clock::now();
      uint8_t v                                        = tf_a_sptr->arr[0];
      v++;
      std::this_thread::sleep_for(std::chrono::nanoseconds(5)); // fake work
      std::chrono::duration<double> elapsed_seconds = end - start;
      sptr->get(fld_read_duration) += elapsed_seconds.count() * 1000000;
    };

    auto writer_test = [sptr, tf_a, fld_write_duration]() {
      auto start = std::chrono::system_clock::now();
      write_ptr<testField> tf_a_sptr(sptr->writeCopySwap(tf_a));
      auto end = std::chrono::system_clock::now();
      tf_a_sptr->arr[0]++;
      std::this_thread::sleep_for(std::chrono::nanoseconds(5)); // fake work, holds write locks til end of scope.
      std::chrono::duration<double> elapsed_seconds = end - start;
      sptr->get(fld_write_duration) += elapsed_seconds.count() * 1000000;
    };

    std::thread writers[N];
    std::thread readers[N];

    for (int i = 0; i < N; i++) {
      writers[i] = std::thread(writer_test);
      readers[i] = std::thread(reader_test);
    }

    for (int i = 0; i < N; i++) {
      writers[i].join();
      readers[i].join();
    }

    uint32_t read_time  = sptr->get(fld_read_duration) / N;
    uint32_t write_time = sptr->get(fld_write_duration) / N;
    INFO("avg reader blocked time = " << read_time << " ns");
    INFO("avg writer blocked time = " << write_time << " ns");
    CHECK(false);
  }
  INFO("Extendible Test Complete")
}
// TODO: write multithreaded tests.
