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

#pragma once

void test_done();

#define TEST_DONE() test_done();
#define T_DONE 1
#define T_CONT 1

#define DEFAULT_URL "http://www.scw00.com/"

#include <cstddef>

class CacheTestBase;

struct TestContChain : public Continuation {
  TestContChain();
  virtual ~TestContChain() { this->next_test(); }

  void
  add(TestContChain *n)
  {
    TestContChain *p = this;
    while (p->next) {
      p = p->next;
    }
    p->next = n;
  }

  bool
  next_test()
  {
    if (!this->next) {
      return false;
    }

    this_ethread()->schedule_imm(this->next);
    return true;
  }

  TestContChain *next = nullptr;
};

class CacheTestHandler : public TestContChain
{
public:
  CacheTestHandler() = default;
  CacheTestHandler(size_t size, const char *url = DEFAULT_URL);

  int start_test(int event, void *e);

  virtual void handle_cache_event(int event, CacheTestBase *base);

protected:
  CacheTestBase *_rt = nullptr;
  CacheTestBase *_wt = nullptr;
};

class TerminalTest : public CacheTestHandler
{
public:
  TerminalTest() { SET_HANDLER(&TerminalTest::terminal_event); }
  ~TerminalTest() { TEST_DONE(); }

  int
  terminal_event(int event, void *e)
  {
    delete this;
    return 0;
  }

  void
  handle_cache_event(int event, CacheTestBase *e) override
  {
    delete this;
  }
};
