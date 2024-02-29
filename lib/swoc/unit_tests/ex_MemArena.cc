/** @file

    MemArena example code.

    Licensed to the Apache Software Foundation (ASF) under one or more contributor license
    agreements.  See the NOTICE file distributed with this work for additional information regarding
    copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
    (the "License"); you may not use this file except in compliance with the License.  You may
    obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software distributed under the
    License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
    express or implied. See the License for the specific language governing permissions and
    limitations under the License.
*/

#include <string_view>
#include <memory>
#include <random>
#include "swoc/BufferWriter.h"
#include "swoc/MemArena.h"
#include "swoc/TextView.h"
#include "swoc/IntrusiveHashMap.h"
#include "swoc/bwf_base.h"
#include "swoc/ext/HashFNV.h"
#include "catch.hpp"

using swoc::MemSpan;
using swoc::MemArena;
using swoc::TextView;
using std::string_view;
using swoc::FixedBufferWriter;
using namespace std::literals;

TextView
localize(MemArena &arena, TextView view) {
  auto span = arena.alloc(view.size()).rebind<char>();
  memcpy(span, view);
  return span;
}

template <typename T> struct Destructor {
  void
  operator()(T *t) {
    t->~T();
  }
};

void
Destroy(MemArena *arena) {
  arena->~MemArena();
}

TEST_CASE("MemArena inversion", "[libswoc][MemArena][example][inversion]") {
  TextView tv{"You done messed up A-A-Ron"};
  TextView text{"SolidWallOfCode"};

  {
    MemArena tmp;
    MemArena *arena = tmp.make<MemArena>(std::move(tmp));
    arena->~MemArena();
  }

  {
    std::unique_ptr<MemArena> arena{new MemArena};

    TextView local_tv = localize(*arena, tv);
    REQUIRE(local_tv == tv);
    REQUIRE(arena->contains(local_tv.data()));
  }

  {
    auto destroyer = [](MemArena *arena) -> void { arena->~MemArena(); };

    MemArena ta;

    TextView local_tv = localize(ta, tv);
    REQUIRE(local_tv == tv);
    REQUIRE(ta.contains(local_tv.data()));

    // 16 bytes.
    std::unique_ptr<MemArena, void (*)(MemArena *)> arena(ta.make<MemArena>(std::move(ta)), destroyer);

    REQUIRE(ta.size() == 0);
    REQUIRE(ta.contains(local_tv.data()) == false);

    REQUIRE(arena->size() >= local_tv.size());
    REQUIRE(local_tv == tv);
    REQUIRE(arena->contains(local_tv.data()));

    TextView local_text = localize(*arena, text);
    REQUIRE(local_text == text);
    REQUIRE(local_tv != local_text);
    REQUIRE(local_tv.data() != local_text.data());
    REQUIRE(arena->contains(local_text.data()));
    REQUIRE(arena->size() >= local_tv.size() + local_text.size());
  }

  {
    MemArena ta;
    // 8 bytes.
    std::unique_ptr<MemArena, Destructor<MemArena>> arena(ta.make<MemArena>(std::move(ta)));
  }

  {
    MemArena ta;
    // 16 bytes
    std::unique_ptr<MemArena, void (*)(MemArena *)> arena(ta.make<MemArena>(std::move(ta)), &Destroy);
  }

  {
    MemArena ta;
    // 16 bytes
    std::unique_ptr<MemArena, void (*)(MemArena *)> arena(ta.make<MemArena>(std::move(ta)),
                                                          [](MemArena *arena) -> void { arena->~MemArena(); });
  }

  {
    auto destroyer = [](MemArena *arena) -> void { arena->~MemArena(); };
    MemArena ta;
    // 8 bytes
    std::unique_ptr<MemArena, decltype(destroyer)> arena(ta.make<MemArena>(std::move(ta)), destroyer);
  }
};

template <typename... Args>
TextView
bw_localize(MemArena &arena, TextView const &fmt, Args &&...args) {
  FixedBufferWriter w(arena.remnant());
  auto arg_tuple{std::forward_as_tuple(args...)};
  w.print_v(fmt, arg_tuple);
  if (w.error()) {
    FixedBufferWriter(arena.require(w.extent()).remnant()).print_v(fmt, arg_tuple);
  }
  return arena.alloc(w.extent()).rebind<char>();
}

TEST_CASE("MemArena example", "[libswoc][MemArena][example]") {
  struct Thing {
    using self_type = Thing;

    int n{10};
    std::string_view name{"name"};

    self_type *_next{nullptr};
    self_type *_prev{nullptr};

    Thing() {}

    Thing(int x) : n(x) {}

    Thing(std::string_view const &s) : name(s) {}

    Thing(int x, std::string_view s) : n(x), name(s) {}

    Thing(std::string_view const &s, int x) : n(x), name(s) {}

    struct Linkage : swoc::IntrusiveLinkage<self_type> {
      static std::string_view
      key_of(self_type *thing) {
        return thing->name;
      }

      static uint32_t
      hash_of(std::string_view const &s) {
        return swoc::Hash32FNV1a().hash_immediate(swoc::transform_view_of(&toupper, s));
      }

      static bool
      equal(std::string_view const &lhs, std::string_view const &rhs) {
        return lhs == rhs;
      }
    };
  };

  MemArena arena;
  TextView text = localize(arena, "Goofy Goober");

  Thing *thing = arena.make<Thing>();
  REQUIRE(thing->name == "name");
  REQUIRE(thing->n == 10);

  thing = arena.make<Thing>(text, 956);
  REQUIRE(thing->name.data() == text.data());
  REQUIRE(thing->n == 956);

  // Consume most of the space left.
  arena.alloc(arena.remaining() - 16);

  FixedBufferWriter w(arena.remnant());
  w.print("Much ado about not much text");
  if (w.error()) {
    FixedBufferWriter lw(arena.require(w.extent()).remnant());
    lw.print("Much ado about not much text");
  }
  auto span = arena.alloc(w.extent()).rebind<char>(); // commit the memory.
  REQUIRE(TextView(span) == "Much ado about not much text");

  auto tv1 = bw_localize(arena, "Text: {} - '{}'", 956, "Additional");
  REQUIRE(tv1 == "Text: 956 - 'Additional'");
  REQUIRE(arena.contains(tv1.data()));

  arena.clear();

  using Map = swoc::IntrusiveHashMap<Thing::Linkage>;
  Map *ihm  = arena.make<Map>();

  {
    std::string key_1{"Key One"};
    std::string key_2{"Key Two"};

    ihm->insert(arena.make<Thing>(localize(arena, key_1), 1));
    ihm->insert(arena.make<Thing>(localize(arena, key_2), 2));
  }

  thing = ihm->find("Key One");
  REQUIRE(thing->name == "Key One");
  REQUIRE(thing->n == 1);
  REQUIRE(arena.contains(ihm));
  REQUIRE(arena.contains(thing));
  REQUIRE(arena.contains(thing->name.data()));
};
