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

#include "tscpp/api/RemapPlugin.h"
#include "tscpp/api/PluginInit.h"
#include "tscpp/api/Logger.h"
#include <vector>
#include <map>
#include <sstream>

using namespace std;
using namespace atscppapi;

#define LOG_TAG "remapplugin"

namespace
{
RemapPlugin *plugin;
}

class MyRemapPlugin : public RemapPlugin
{
public:
  explicit MyRemapPlugin(void **instance_handle) : RemapPlugin(instance_handle) {}

  Result
  doRemap(const Url &map_from_url, const Url &map_to_url, Transaction &transaction, bool &redirect) override
  {
    Url &request_url = transaction.getClientRequest().getUrl();
    TS_DEBUG(LOG_TAG, "from URL is [%s], to URL is [%s], request URL is [%s]", map_from_url.getUrlString().c_str(),
             map_to_url.getUrlString().c_str(), request_url.getUrlString().c_str());
    const string &query = request_url.getQuery();
    string query_param_raw;
    map<string, string> query_params;
    std::istringstream iss(query);
    while (std::getline(iss, query_param_raw, '&')) {
      size_t equals_pos = query_param_raw.find('=');
      if (equals_pos && (equals_pos < (query_param_raw.size() - 1))) {
        query_params[string(query_param_raw, 0, equals_pos)] =
          string(query_param_raw, equals_pos + 1, query_param_raw.size() - equals_pos - 1);
      }
    }
    if (query_params.count("error")) {
      return RESULT_ERROR;
    }
    const string &remap = query_params["remap"];
    bool stop           = (query_params["stop"] == "true");
    Result result       = stop ? RESULT_NO_REMAP_STOP : RESULT_NO_REMAP;
    if (remap == "true") {
      const string &path = query_params["path"];
      if (path.size()) {
        request_url.setPath(path);
      }
      const string &host = query_params["host"];
      if (host.size()) {
        request_url.setHost(host);
      }
      const string &port_str = query_params["port"];
      if (port_str.size()) {
        uint16_t port;
        iss.str(port_str);
        iss >> port;
        request_url.setPort(port);
      }
      if (query_params.count("redirect")) {
        redirect = true;
      }
      result = stop ? RESULT_DID_REMAP_STOP : RESULT_DID_REMAP;
    }
    request_url.setQuery("");
    TS_DEBUG(LOG_TAG, "Request URL is now [%s]", request_url.getUrlString().c_str());
    return result;
  }
};

TSReturnCode
TSRemapNewInstance(int argc ATSCPPAPI_UNUSED, char *argv[] ATSCPPAPI_UNUSED, void **instance_handle, char *errbuf ATSCPPAPI_UNUSED,
                   int errbuf_size ATSCPPAPI_UNUSED)
{
  plugin = new MyRemapPlugin(instance_handle);
  return TS_SUCCESS;
}
