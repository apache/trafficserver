/** @file

    Allocator tests.

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

#include "tscore/Allocator.h"

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>

// Counter to track constructor/destructor calls
static int g_construct_count = 0;
static int g_destruct_count  = 0;

// Simple POD type
struct SimplePOD {
  int    x;
  double y;
  void  *ptr;
};

// Type with non-trivial destructor that tracks calls
// Must be at least sizeof(void*) for ClassAllocator
struct TrackedObject {
  int   value;
  void *padding; // Ensure size >= sizeof(void*)

  TrackedObject(int v = 0) : value(v), padding(nullptr) { g_construct_count++; }

  ~TrackedObject() { g_destruct_count++; }
};

// Type with resource management (RAII)
struct ResourceHolder {
  std::unique_ptr<int> resource;
  bool                *destroyed_flag;

  ResourceHolder(int val, bool *flag) : resource(std::make_unique<int>(val)), destroyed_flag(flag) {}

  ~ResourceHolder()
  {
    if (destroyed_flag) {
      *destroyed_flag = true;
    }
  }
};

// Type with complex state
struct ComplexObject {
  std::string name;
  int         id;
  double      data[10];

  ComplexObject(std::string n, int i) : name(std::move(n)), id(i)
  {
    for (int j = 0; j < 10; ++j) {
      data[j] = i * j;
    }
  }

  ~ComplexObject() = default;
};

// Type with explicit cleanup
struct CleanupTracker {
  int *counter;

  explicit CleanupTracker(int *c) : counter(c)
  {
    if (counter) {
      (*counter)++;
    }
  }

  ~CleanupTracker()
  {
    if (counter) {
      (*counter)--;
    }
  }

  // Delete copy operations to ensure proper lifecycle
  CleanupTracker(const CleanupTracker &)            = delete;
  CleanupTracker &operator=(const CleanupTracker &) = delete;
};

TEST_CASE("ClassAllocator basic allocation", "[libts][allocator]")
{
  ClassAllocator<SimplePOD, false> allocator("test_simple_pod");

  SECTION("Allocate and free simple POD")
  {
    SimplePOD *obj = allocator.alloc();
    REQUIRE(obj != nullptr);

    obj->x   = 42;
    obj->y   = 3.14;
    obj->ptr = nullptr;

    REQUIRE(obj->x == 42);
    REQUIRE(obj->y == 3.14);

    allocator.free(obj);
  }

  SECTION("Allocate multiple objects")
  {
    constexpr int            count = 10;
    std::vector<SimplePOD *> objects;

    for (int i = 0; i < count; ++i) {
      SimplePOD *obj = allocator.alloc();
      REQUIRE(obj != nullptr);
      obj->x = i;
      objects.push_back(obj);
    }

    for (int i = 0; i < count; ++i) {
      REQUIRE(objects[i]->x == i);
    }

    for (auto *obj : objects) {
      allocator.free(obj);
    }
  }
}

TEST_CASE("ClassAllocator destructor calls", "[libts][allocator]")
{
  SECTION("Destructor called on free")
  {
    g_construct_count = 0;
    g_destruct_count  = 0;

    ClassAllocator<TrackedObject> allocator("test_tracked");

    TrackedObject *obj = allocator.alloc(42);
    REQUIRE(obj != nullptr);
    REQUIRE(obj->value == 42);
    REQUIRE(g_construct_count == 1);
    REQUIRE(g_destruct_count == 0);

    allocator.free(obj);
    REQUIRE(g_destruct_count == 1);
  }

  SECTION("Multiple destructor calls")
  {
    g_construct_count = 0;
    g_destruct_count  = 0;

    ClassAllocator<TrackedObject> allocator("test_tracked_multi");

    constexpr int                count = 5;
    std::vector<TrackedObject *> objects;

    for (int i = 0; i < count; ++i) {
      objects.push_back(allocator.alloc(i));
    }

    REQUIRE(g_construct_count == count);
    REQUIRE(g_destruct_count == 0);

    for (auto *obj : objects) {
      allocator.free(obj);
    }

    REQUIRE(g_destruct_count == count);
  }
}

TEST_CASE("ClassAllocator with RAII types", "[libts][allocator]")
{
  ClassAllocator<ResourceHolder> allocator("test_resource_holder");

  SECTION("RAII cleanup on free")
  {
    bool destroyed = false;

    ResourceHolder *obj = allocator.alloc(123, &destroyed);
    REQUIRE(obj != nullptr);
    REQUIRE(obj->resource != nullptr);
    REQUIRE(*obj->resource == 123);
    REQUIRE(destroyed == false);

    allocator.free(obj);
    REQUIRE(destroyed == true);
  }

  SECTION("Multiple RAII objects")
  {
    constexpr int                 count                  = 3;
    bool                          destroyed_flags[count] = {false, false, false};
    std::vector<ResourceHolder *> objects;

    for (int i = 0; i < count; ++i) {
      objects.push_back(allocator.alloc(i * 100, &destroyed_flags[i]));
    }

    for (int i = 0; i < count; ++i) {
      REQUIRE(destroyed_flags[i] == false);
    }

    for (auto *obj : objects) {
      allocator.free(obj);
    }

    for (int i = 0; i < count; ++i) {
      REQUIRE(destroyed_flags[i] == true);
    }
  }
}

TEST_CASE("ClassAllocator with complex types", "[libts][allocator]")
{
  ClassAllocator<ComplexObject> allocator("test_complex");

  SECTION("Complex object with std::string")
  {
    ComplexObject *obj = allocator.alloc("test_object", 7);
    REQUIRE(obj != nullptr);
    REQUIRE(obj->name == "test_object");
    REQUIRE(obj->id == 7);
    REQUIRE(obj->data[5] == 35.0); // 7 * 5

    allocator.free(obj);
    // If destructor isn't called, this would leak the std::string
  }

  SECTION("Multiple complex objects")
  {
    std::vector<ComplexObject *> objects;
    std::string prefix = "this needs to be long to avoid short string optimizations, hopefully this is enough: obj_";

    for (int i = 0; i < 5; ++i) {
      objects.push_back(allocator.alloc(prefix + std::to_string(i), i));
    }

    for (int i = 0; i < 5; ++i) {
      REQUIRE(objects[i]->name == prefix + std::to_string(i));
      REQUIRE(objects[i]->id == i);
    }

    for (auto *obj : objects) {
      allocator.free(obj);
    }
  }
}

TEST_CASE("ClassAllocator cleanup tracking", "[libts][allocator]")
{
  ClassAllocator<CleanupTracker> allocator("test_cleanup");

  SECTION("Cleanup counter decremented on free")
  {
    int counter = 0;

    CleanupTracker *obj = allocator.alloc(&counter);
    REQUIRE(obj != nullptr);
    REQUIRE(counter == 1);

    allocator.free(obj);
    REQUIRE(counter == 0);
  }

  SECTION("Multiple cleanup objects")
  {
    int                           counter = 0;
    std::vector<CleanupTracker *> objects;

    for (int i = 0; i < 10; ++i) {
      objects.push_back(allocator.alloc(&counter));
    }

    REQUIRE(counter == 10);

    for (auto *obj : objects) {
      allocator.free(obj);
    }

    REQUIRE(counter == 0);
  }
}

TEST_CASE("ClassAllocator constructor forwarding", "[libts][allocator]")
{
  ClassAllocator<ComplexObject> allocator("test_forwarding");

  SECTION("Perfect forwarding of constructor arguments")
  {
    std::string    name = "forwarded";
    ComplexObject *obj  = allocator.alloc(name, 99);

    REQUIRE(obj != nullptr);
    REQUIRE(obj->name == "forwarded");
    REQUIRE(obj->id == 99);

    allocator.free(obj);
  }

  SECTION("Move semantics")
  {
    ComplexObject *obj = allocator.alloc(std::string("moved"), 42);

    REQUIRE(obj != nullptr);
    REQUIRE(obj->name == "moved");
    REQUIRE(obj->id == 42);

    allocator.free(obj);
  }
}
