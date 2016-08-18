/** @file

  A brief file description

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

#include "ts/List.h"

class Foo
{
public:
  int x;

  void
  foo()
  {
  }

  SLINK(Foo, slink);
  LINK(Foo, dlink);

  Foo(int i = 0) : x(i) {}
};

int
main()
{
  SList(Foo, slink) s;
  DList(Foo, dlink) d;
  Que(Foo, dlink) q;
  Foo *f = new Foo;
  f->x   = 7;
  s.push(f);
  d.push(s.pop());
  q.enqueue(d.pop());
  for (int i = 0; i < 100; i++) {
    q.enqueue(new Foo(i));
  }
  int tot = 0;
  for (int i = 0; i < 101; i++) {
    Foo *foo = q.dequeue();
    tot += foo->x;
    delete foo;
  }
  if (tot != 4957) {
    printf("test_List FAILED\n");
    exit(1);
  } else {
    printf("test_List PASSED\n");
    exit(0);
  }
}
