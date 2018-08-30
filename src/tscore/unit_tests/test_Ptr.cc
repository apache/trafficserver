/*

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

#include "tscore/Ptr.h"
#include "catch.hpp"

struct PtrObject : RefCountObj {
  PtrObject(unsigned *_c) : count(_c) { ++(*count); }
  ~PtrObject() override { --(*count); }
  unsigned *count;
};

TEST_CASE("Ptr", "[libts][ptr]")
{
  unsigned alive{0};

  Ptr<PtrObject> p1 = make_ptr(new PtrObject(&alive));
  PtrObject *p2     = p1.detach();

  REQUIRE(p1.get() == nullptr);
  REQUIRE(p2->refcount() == 1);

  // Note that there's no symmetric attach.
  p1 = p2;
  REQUIRE(p2->refcount() == 2);
  p1 = p1;
  REQUIRE(p1->refcount() == 2);
  p2->refcount_dec();
  delete p1.detach();
  // If that doesn't work, the subsequent alive counts will be off.

  p1 = make_ptr(new PtrObject(&alive));
  REQUIRE(alive == 1);
  p1.clear();
  REQUIRE(p1.get() == nullptr);
  REQUIRE(alive == 0);

  p1 = make_ptr(new PtrObject(&alive));
  REQUIRE(alive == 1);
  p1 = nullptr;
  REQUIRE(alive == 0);

  { // check scope based cleanup.
    Ptr<PtrObject> pn1 = make_ptr(new PtrObject(&alive));

    REQUIRE(pn1->refcount() == 1);

    Ptr<PtrObject> pn2(pn1);
    REQUIRE(pn1->refcount() == 2);

    Ptr<PtrObject> pn3 = p1;
  }

  // Everything goes out of scope, so the refcounts should drop to zero.
  REQUIRE(alive == 0);

  Ptr<PtrObject> none;
  Ptr<PtrObject> some = make_ptr(new PtrObject(&alive));

  REQUIRE(!none);
  REQUIRE(bool(some) == true);
}
