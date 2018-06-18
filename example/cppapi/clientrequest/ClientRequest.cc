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

#include <iostream>
#include <atscppapi/GlobalPlugin.h>
#include <atscppapi/PluginInit.h>
#include <atscppapi/utils.h>

using namespace atscppapi;

using std::cout;
using std::endl;
using std::list;
using std::string;

namespace
{
GlobalPlugin *plugin;
}

class GlobalHookPlugin : public GlobalPlugin
{
public:
  GlobalHookPlugin()
  {
    registerHook(HOOK_READ_REQUEST_HEADERS_PRE_REMAP);
    registerHook(HOOK_READ_REQUEST_HEADERS_POST_REMAP);
    registerHook(HOOK_SEND_REQUEST_HEADERS);
  }

  void
  handleReadRequestHeadersPreRemap(Transaction &transaction) override
  {
    cout << "Hello from handleReadRequesHeadersPreRemap!" << endl;

    ClientRequest &client_request = transaction.getClientRequest();
    Url &request_url              = client_request.getUrl();

    cout << "Method is " << HTTP_METHOD_STRINGS[client_request.getMethod()] << endl;
    cout << "Version is " << HTTP_VERSION_STRINGS[client_request.getVersion()] << endl;
    cout << "---------------------------------------------------" << endl;
    cout << "URL is " << request_url.getUrlString() << endl;
    cout << "Path is " << request_url.getPath() << endl;
    cout << "Query is " << request_url.getQuery() << endl;
    cout << "Host is " << request_url.getHost() << endl;
    cout << "Port is " << request_url.getPort() << endl;
    cout << "Scheme is " << request_url.getScheme() << endl;
    cout << "---------------------------------------------------" << endl;
    if (request_url.getPath() == "remap_me") {
      request_url.setPath("index.html");
    }

    transaction.resume();
  }

  void
  handleReadRequestHeadersPostRemap(Transaction &transaction) override
  {
    cout << "Hello from handleReadRequesHeadersPostRemap!" << endl;

    ClientRequest &client_request   = transaction.getClientRequest();
    Url &request_url                = client_request.getUrl();
    const Url &pristine_request_url = client_request.getPristineUrl();

    cout << "--------------------PRISTINE-----------------------" << endl;
    cout << "URL is " << pristine_request_url.getUrlString() << endl;
    cout << "Path is " << pristine_request_url.getPath() << endl;
    cout << "--------------------POST REMAP---------------------" << endl;
    cout << "URL is " << request_url.getUrlString() << endl;
    cout << "Path is " << request_url.getPath() << endl;
    cout << "---------------------------------------------------" << endl;

    Headers &client_request_headers = client_request.getHeaders();

    Headers::iterator ii = client_request_headers.find("AccepT-EncodinG");
    if (ii != client_request_headers.end()) {
      cout << "Deleting accept-encoding header" << endl;
      client_request_headers.erase("AccepT-EnCoDing"); // Case Insensitive
    }

    // These will be split back up into a list of three values automatically (see header output below).
    cout << "Adding back Accept-Encoding." << endl;
    client_request_headers["accept-encoding"] = "gzip, identity";
    client_request_headers.append("accept-ENCODING", "my_special_format");

    cout << "Adding a new accept type accept header" << endl;
    client_request_headers.append("accept", "text/blah");

    for (auto &&client_request_header : client_request_headers) {
      cout << client_request_header.str() << endl;
    }

    /*
     * These will output:
     * Joining on a non-existent header gives:
     * Joining the accept encoding header gives: gzip,identity,my_special_format
     * Joining the accept encoding header with space gives: gzip identity my_special_format
     */
    cout << "Joining on a non-existent header gives: " << client_request_headers.values("i_dont_exist") << endl;
    cout << "Joining the accept encoding header gives: " << client_request_headers.values("accept-encoding") << endl;
    cout << "Joining the accept encoding header with space gives: " << client_request_headers.values("accept-encoding", ' ')
         << endl;
    cout << "Joining the accept encoding header with long join string gives: "
         << client_request_headers.values("accept-encoding", "--join-string--") << endl;

    transaction.resume();
  }

  void
  handleSendRequestHeaders(Transaction &transaction) override
  {
    cout << "Hello from handleSendRequestHeaders!" << endl;
    cout << "---------------------IP INFORMATION-----------------" << endl;
    cout << "Server Address: " << utils::getIpPortString(transaction.getServerAddress()) << endl;
    cout << "Incoming Address: " << utils::getIpPortString(transaction.getIncomingAddress()) << endl;
    cout << "Client Address: " << utils::getIpPortString(transaction.getClientAddress()) << endl;
    cout << "Next Hop Address: " << utils::getIpPortString(transaction.getNextHopAddress()) << endl;
    transaction.resume();
  }
};

void
TSPluginInit(int argc ATSCPPAPI_UNUSED, const char *argv[] ATSCPPAPI_UNUSED)
{
  if (!RegisterGlobalPlugin("CPP_Example_ClientRequest", "apache", "dev@trafficserver.apache.org")) {
    return;
  }
  plugin = new GlobalHookPlugin();
}
