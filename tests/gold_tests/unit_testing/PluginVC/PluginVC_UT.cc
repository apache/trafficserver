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

/****************************************************************************

   Description: Unit testing for proxy/PluginVC.cc.

 ****************************************************************************/

#include <PluginVC.h>
#include <P_EventSystem.h>
#include <P_Net.h>
#include "NetVCTest.h"

#include "../unit_testing.h"

namespace
{
class PVCTestDriver : public NetTestDriver
{
public:
  PVCTestDriver(InProgress const &ip);
  ~PVCTestDriver() override;

  void start_tests();
  void run_next_test();
  int main_handler(int event, void *data);

private:
  unsigned i                    = 0;
  unsigned completions_received = 0;
  InProgress _ip;
};

PVCTestDriver::PVCTestDriver(InProgress const &ip) : _ip(ip) {}

PVCTestDriver::~PVCTestDriver()
{
  mutex = nullptr;
}

void
PVCTestDriver::start_tests()
{
  mutex = new_ProxyMutex();
  MUTEX_TRY_LOCK(lock, mutex, this_ethread());

  SET_HANDLER(&PVCTestDriver::main_handler);

  run_next_test();
}

void
PVCTestDriver::run_next_test()
{
  unsigned a_index = i * 2;
  unsigned p_index = a_index + 1;

  if (p_index >= num_netvc_tests) {
    // We are done - PASS or FAIL?
    ink_release_assert(errors == 0);
    delete this;
    return;
  }
  completions_received = 0;
  i++;

  TSDebug(Debug_tag, "PVCTestDriver: Starting test %s", netvc_tests_def[a_index].test_name);

  NetVCTest *p       = new NetVCTest;
  NetVCTest *a       = new NetVCTest;
  PluginVCCore *core = PluginVCCore::alloc(p);

  p->init_test(NET_VC_TEST_PASSIVE, this, nullptr, &netvc_tests_def[p_index], "PluginVC");
  PluginVC *a_vc = core->connect();

  a->init_test(NET_VC_TEST_ACTIVE, this, a_vc, &netvc_tests_def[a_index], "PluginVC");
}

int
PVCTestDriver::main_handler(int /* event ATS_UNUSED */, void * /* data ATS_UNUSED */)
{
  completions_received++;

  if (completions_received == 2) {
    run_next_test();
  }

  return 0;
}

void
test(InProgress ip)
{
  PVCTestDriver *driver = new PVCTestDriver(ip);
  driver->start_tests();
}

Test t(test);

} // end anonymous namespace
