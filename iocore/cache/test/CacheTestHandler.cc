/** @file

  A brief file description

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
 1 test_Cache.cc + X distributed with this work for additional information regarding copyright ownership.  The ASF licenses this
 file to you under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

#include "main.h"
#include "CacheTestHandler.h"

TestContChain::TestContChain() : Continuation(new_ProxyMutex()) {}

CacheTestHandler::CacheTestHandler(size_t size, const char *url)
{
  this->_wt = new CacheWriteTest(size, this, url);
  this->_rt = new CacheReadTest(size, this, url);

  this->_wt->mutex = this->mutex;
  this->_rt->mutex = this->mutex;
  SET_HANDLER(&CacheTestHandler::start_test);
}

void
CacheTestHandler::handle_cache_event(int event, CacheTestBase *base)
{
  REQUIRE(base != nullptr);
  switch (event) {
  case CACHE_EVENT_OPEN_READ:
    base->do_io_read();
    break;
  case CACHE_EVENT_OPEN_WRITE:
    base->do_io_write();
    break;
  case VC_EVENT_READ_READY:
  case VC_EVENT_WRITE_READY:
    REQUIRE(base->vc != nullptr);
    REQUIRE(base->vio != nullptr);
    base->reenable();
    break;
  case VC_EVENT_WRITE_COMPLETE:
    this_ethread()->schedule_imm(this->_rt);
    base->close();
    break;
  case VC_EVENT_READ_COMPLETE:
    base->close();
    delete this;
    break;
  default:
    REQUIRE(false);
    base->close();
    delete this;
    break;
  }
  return;
}

int
CacheTestHandler::start_test(int event, void *e)
{
  this_ethread()->schedule_imm(this->_wt);
  return 0;
}
