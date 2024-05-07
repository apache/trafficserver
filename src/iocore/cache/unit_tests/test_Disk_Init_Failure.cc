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

#include "main.h"

#define LARGE_FILE 10 * 1024 * 1024
#define SMALL_FILE 10 * 1024

#ifndef AIO_FAULT_INJECTION
#error Must define AIO_FAULT_INJECTION!
#endif
#include "iocore/aio/AIO_fault_injection.h"

int        cache_vols           = 2;
bool       reuse_existing_cache = false;
extern int gndisks;

class CacheCommInit : public CacheInit
{
public:
  CacheCommInit() {}
  int
  cache_init_success_callback(int event, void *e) override
  {
    // We initialize two disks and inject failure in one.  Ensure that one disk
    // remains if the fault is during initialization.
    REQUIRE(gndisks == 1);

    CacheTestHandler *h  = new CacheTestHandler(LARGE_FILE);
    CacheTestHandler *h2 = new CacheTestHandler(SMALL_FILE, "http://www.scw11.com");
    TerminalTest     *tt = new TerminalTest;
    h->add(h2);
    h->add(tt);
    this_ethread()->schedule_imm(h);
    delete this;
    return 0;
  }
};

TEST_CASE("Cache disk initialization fail", "cache")
{
  std::vector<int> indices = FAILURE_INDICES;
  for (const int i : indices) {
    aioFaultInjection.inject_fault(".*/var/trafficserver2/cache.db", i, {.err_no = EIO, .skip_io = true});
  }
  init_cache(256 * 1024 * 1024);
  // large write test
  CacheCommInit *init = new CacheCommInit;

  this_ethread()->schedule_imm(init);
  this_thread()->execute();
}
