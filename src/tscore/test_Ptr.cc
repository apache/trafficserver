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
#include "tscore/TestBox.h"

struct PtrObject : RefCountObj {
  PtrObject(unsigned *_c) : count(_c) { ++(*count); }
  ~PtrObject() override { --(*count); }
  unsigned *count;
};

REGRESSION_TEST(Ptr_detach)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);
  box            = REGRESSION_TEST_PASSED;
  unsigned alive = 0;

  Ptr<PtrObject> p1 = make_ptr(new PtrObject(&alive));
  PtrObject *p2     = p1.detach();

  box.check(p1.get() == nullptr, "Ptr<T>::detach NULLs the stored pointer");
  box.check(p2->refcount() == 1, "Ptr<T>::detach preserves the refcount");

  // Note that there's no symmetric reattach.
  p1 = p2;
  box.check(p2->refcount() == 2, "reattaching increments the refcount again");
  p1->refcount_dec();
}

REGRESSION_TEST(Ptr_clear)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);
  box            = REGRESSION_TEST_PASSED;
  unsigned alive = 0;

  Ptr<PtrObject> p1 = make_ptr(new PtrObject(&alive));
  box.check(alive == 1, "we have a live object");
  p1.clear();
  box.check(p1.get() == nullptr, "Ptr<T>::clear NULLs the pointer");
  box.check(alive == 0, "Ptr<T>::clear drops the refcount");

  p1 = make_ptr(new PtrObject(&alive));
  box.check(alive == 1, "we have a live object");
  p1 = nullptr;
  box.check(alive == 0, "assigning NULL drops the refcount");
}

REGRESSION_TEST(Ptr_refcount)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);
  box            = REGRESSION_TEST_PASSED;
  unsigned alive = 0;

  {
    Ptr<PtrObject> p1 = make_ptr(new PtrObject(&alive));

    box.check(p1->refcount() == 1, "initial refcount is 1");

    Ptr<PtrObject> p2(p1);
    box.check(p1->refcount() == 2, "initial refcount is 1");

    Ptr<PtrObject> p3 = p1;
  }

  // Everything goes out of scope, so the refcounts should drop to zero.
  box.check(alive == 0, "refcounts dropped");
}

REGRESSION_TEST(Ptr_bool)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);
  box            = REGRESSION_TEST_PASSED;
  unsigned alive = 0;

  Ptr<PtrObject> none;
  Ptr<PtrObject> some = make_ptr(new PtrObject(&alive));

  box.check(!none, "Empty Ptr<T> is false");
  box.check(bool(some), "Non-empty Ptr<T> is true");
}
