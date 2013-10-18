/**
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

#include <atscppapi/PluginInit.h>
#include <atscppapi/GlobalPlugin.h>
#include <atscppapi/Logger.h>

using namespace atscppapi;
using std::string;

#define LOG_TAG "request_cookies"

class MyGlobalPlugin : GlobalPlugin {
public:
  MyGlobalPlugin() {
    GlobalPlugin::registerHook(Plugin::HOOK_READ_REQUEST_HEADERS_PRE_REMAP);
  }
private:
  void handleReadRequestHeadersPreRemap(Transaction &transaction) {
    Headers &headers = transaction.getClientRequest().getHeaders();
    TS_DEBUG(LOG_TAG, "Read request");
    logRequestCookies(headers);
    headers.addCookie("gen-c1", "gen-v2");
    TS_DEBUG(LOG_TAG, "Added cookie");
    logRequestCookies(headers);
    headers.setCookie("c1", "correctv");
    TS_DEBUG(LOG_TAG, "Set cookie");
    logRequestCookies(headers);
    headers.deleteCookie("gen-c1");
    TS_DEBUG(LOG_TAG, "Deleted cookie");
    logRequestCookies(headers);
    transaction.resume();
  }

  void logRequestCookies(Headers &headers) {
    TS_DEBUG(LOG_TAG, "Cookie header is [%s]", headers.getJoinedValues("Cookie").c_str());
    string map_str;
    const Headers::RequestCookieMap &cookie_map = headers.getRequestCookies();
    for (Headers::RequestCookieMap::const_iterator cookie_iter = cookie_map.begin(), cookie_end = cookie_map.end();
         cookie_iter != cookie_end; ++cookie_iter) {
      map_str += cookie_iter->first;
      map_str += ": ";
      map_str += Headers::getJoinedValues(cookie_iter->second);
      map_str += "\n";
    }
    TS_DEBUG(LOG_TAG, "Cookie map is\n%s", map_str.c_str());
  }
};

void TSPluginInit(int argc, const char *argv[]) {
  new MyGlobalPlugin();
}
