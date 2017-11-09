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

#include "HttpConnectionCount.h"

ConnectionCount ConnectionCount::_connectionCount;
ConnectionCountQueue ConnectionCountQueue::_connectionCount;

std::string
ConnectionCount::dumpToJSON()
{
  ink_mutex_acquire(&_mutex);
  std::ostringstream oss;
  size_t n = _hostCount.size();
  size_t i = 0;
  oss << '{';
  appendJSONPair(oss, "connectionCountSize", n);
  oss << ", \"connectionCountList\": [";

  for (auto &it : _hostCount) {
    oss << '{';

    appendJSONPair(oss, "ip", it.first.getIpStr());
    oss << ',';

    appendJSONPair(oss, "port", it.first._addr.host_order_port());
    oss << ',';

    appendJSONPair(oss, "hostname_hash", it.first.getHostnameHashStr());
    oss << ',';

    appendJSONPair(oss, "connection_count", it.second);
    oss << "}";

    if (i < n - 1)
      oss << ',';
    i++;
  }

  ink_mutex_release(&_mutex);
  oss << "]}";
  return oss.str();
}

struct ShowConnectionCount : public ShowCont {
  ShowConnectionCount(Continuation *c, HTTPHdr *h) : ShowCont(c, h) { SET_HANDLER(&ShowConnectionCount::showHandler); }
  int
  showHandler(int event, Event *e)
  {
    CHECK_SHOW(show(ConnectionCount::getInstance()->dumpToJSON().c_str()));
    return completeJson(event, e);
  }
};

Action *
register_ShowConnectionCount(Continuation *c, HTTPHdr *h)
{
  ShowConnectionCount *s = new ShowConnectionCount(c, h);
  this_ethread()->schedule_imm(s);
  return &s->action;
}
