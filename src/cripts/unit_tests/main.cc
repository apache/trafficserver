/*
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

// Stands in for traffic_server: the test cases are a module loaded the way a cript is. libcripts' static initializers
// read state the core sets up at startup, before any plugin is loaded: the overridable configuration table, and the
// API's well-known strings that the cripts::Method constants copy. Linking libcripts into this executable would run
// them first.

#include "api/InkAPIInternal.h"
#include "iocore/eventsystem/EThread.h"
#include "proxy/hdrs/HTTP.h"
#include "tscore/BaseLogFile.h"
#include "tscore/Diags.h"

#include <catch2/catch_session.hpp>

#include <dlfcn.h>
#include <cstdio>

extern int cmd_disable_pfreelist;

int
main(int argc, char *argv[])
{
  // No event threads, so keep the allocators off the thread local free lists. THREAD_ALLOC() and TSContCreate() still
  // need a current thread.
  cmd_disable_pfreelist = true;
  Thread *main_thread   = new EThread();
  main_thread->set_specific();
  DiagsPtr::set(new Diags("test_cripts", "" /* tags */, "" /* actions */, new BaseLogFile("stderr")));
  http_init();
  api_init();

  // The cases register with Catch2 as the module loads; libCatch2 is shared, so this executable sees them.
  if (dlopen(CRIPTS_TEST_CASES_PATH, RTLD_NOW | RTLD_GLOBAL) == nullptr) {
    std::fprintf(stderr, "dlopen(%s): %s\n", CRIPTS_TEST_CASES_PATH, dlerror());
    return 1;
  }

  return Catch::Session().run(argc, argv);
}
