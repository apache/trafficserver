/** @file

  A plugin to parse Link headers from an origin server's response and initiate H2 Server Push for preload links.

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

#include <iostream>
#include <regex>
#include <set>
#include <sstream>
#include <ts/ts.h>
#include <ts/experimental.h>
#include <atscppapi/GlobalPlugin.h>
#include <atscppapi/utils.h>

#define PLUGIN_NAME "server_push_preload"
#define PRELOAD_PARAM "rel=preload"
#define NOPUSH_OPTION "nopush"

using namespace std;
using namespace atscppapi;

static regex linkRegexp("<([^>]+)>;(.+)");

namespace
{
GlobalPlugin *plugin;
}

class LinkServerPushPlugin : public GlobalPlugin
{
public:
  LinkServerPushPlugin()
  {
    TSDebug(PLUGIN_NAME, "registering transaction hooks");
    LinkServerPushPlugin::registerHook(HOOK_SEND_RESPONSE_HEADERS);
  }

  void
  handleSendResponseHeaders(Transaction &transaction) override
  {
    serverPush(transaction);
    transaction.resume();
  }

  void
  serverPush(Transaction &transaction)
  {
    TSHttpTxn txnp = static_cast<TSHttpTxn>(transaction.getAtsHandle());
    if (TSHttpTxnClientProtocolStackContains(txnp, "h2") == nullptr) {
      return;
    }

    ClientRequest &request = transaction.getClientRequest();
    Response &response     = transaction.getClientResponse();
    Headers &headers       = response.getHeaders();

    const Url &clientUrl = request.getPristineUrl();

    for (header_field_iterator it = headers.find("Link"); it != headers.end(); it.nextDup()) {
      HeaderField field = *it;

      for (header_field_value_iterator hit = field.begin(); hit != field.end(); ++hit) {
        const string &link = *hit;

        TSDebug(PLUGIN_NAME, "Parsing link header: %s", link.c_str());
        smatch matches;

        if (regex_search(link, matches, linkRegexp)) {
          string url = matches[1].str();
          TSDebug(PLUGIN_NAME, "Found link header match: %s", url.c_str());

          set<string> params = split(matches[2].str(), ';');
          auto preload       = params.find(PRELOAD_PARAM);
          if (preload == params.end()) {
            continue;
          }

          auto noPush = params.find(NOPUSH_OPTION);
          if (noPush != params.end()) {
            TSDebug(PLUGIN_NAME, "Skipping nopush link: %s", link.c_str());
            continue;
          }

          Request request(url);
          Url &linkUrl = request.getUrl();

          if (linkUrl.getHost().empty()) {
            linkUrl.setHost(clientUrl.getHost());
            linkUrl.setScheme(clientUrl.getScheme());
          }
          if (0 != clientUrl.getPort()) {
            linkUrl.setPort(clientUrl.getPort());
          }
          string lu = linkUrl.getUrlString();
          TSDebug(PLUGIN_NAME, "Push preloaded content: %s", lu.c_str());
          TSHttpTxnServerPush(txnp, lu.c_str(), lu.length());
        } else {
          TSDebug(PLUGIN_NAME, "No match found for link header: %s", link.c_str());
        }
      }
    }
  }

  set<string>
  split(const string &params, char delim)
  {
    stringstream ss(params);
    string s;
    set<string> tokens;
    while (getline(ss, s, delim)) {
      s.erase(find_if(s.rbegin(), s.rend(), [](unsigned char c) { return !std::isspace(c); }).base(), s.end()); // trim left
      s.erase(s.begin(), find_if(s.begin(), s.end(), [](unsigned char c) { return !std::isspace(c); }));        // trim right
      tokens.insert(s);
    }
    return tokens;
  }
};

void
TSPluginInit(int argc ATSCPPAPI_UNUSED, const char *argv[] ATSCPPAPI_UNUSED)
{
  TSDebug(PLUGIN_NAME, "Init");
  if (!RegisterGlobalPlugin("ServerPushPreloadPlugin", PLUGIN_NAME, "dev@trafficserver.apache.org")) {
    return;
  }
  plugin = new LinkServerPushPlugin();
}
