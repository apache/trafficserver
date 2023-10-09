/** @file

  This file used for catch based tests. It is the main() stub.

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

#include "HTTP.h"
#include "HuffmanCodec.h"

#define CATCH_CONFIG_RUNNER
#include "catch.hpp"

extern int cmd_disable_pfreelist;

int
main(int argc, char *argv[])
{
  // No thread setup, forbid use of thread local allocators.
  cmd_disable_pfreelist = true;
  // Get all of the HTTP WKS items populated.
  http_init();
  hpack_huffman_init();

  int result = Catch::Session().run(argc, argv);

  // global clean-up...
  hpack_huffman_fin();

  return result;
}

// Stubs

#include <ts/DbgCtl.h>

std::atomic<int> DbgCtl::_config_mode{0};

DbgCtl::_TagData const *
DbgCtl::_new_reference(char const *)
{
  return &_No_tag_dummy();
}

void
DbgCtl::print(char const *, char const *, char const *, int, char const *, ...)
{
}

void
DbgCtl::_rm_reference()
{
}

bool
DbgCtl::_override_global_on()
{
  return false;
}

void
DbgCtl::update()
{
}
