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

#include <atscppapi/GlobalPlugin.h>
#include <atscppapi/InterceptPlugin.h>
#include <atscppapi/PluginInit.h>

#include <iostream>

using namespace atscppapi;
using std::string;
using std::cout;
using std::endl;

namespace
{
GlobalPlugin *plugin;
}

class Intercept : public InterceptPlugin
{
public:
  Intercept(Transaction &transaction) : InterceptPlugin(transaction, InterceptPlugin::SERVER_INTERCEPT) {}
  void consume(const string &data, InterceptPlugin::RequestDataType type) override;
  void handleInputComplete() override;
  ~Intercept() override { cout << "Shutting down" << endl; }
};

class InterceptInstaller : public GlobalPlugin
{
public:
  InterceptInstaller() : GlobalPlugin(true /* ignore internal transactions */)
  {
    GlobalPlugin::registerHook(Plugin::HOOK_READ_REQUEST_HEADERS_PRE_REMAP);
  }
  void
  handleReadRequestHeadersPreRemap(Transaction &transaction) override
  {
    transaction.addPlugin(new Intercept(transaction));
    cout << "Added intercept" << endl;
    transaction.resume();
  }
};

void
TSPluginInit(int /* argc ATS_UNUSED */, const char * /* argv ATS_UNUSED */ [])
{
  if (!RegisterGlobalPlugin("CPP_Example_Intercept", "apache", "dev@trafficserver.apache.org")) {
    return;
  }
  plugin = new InterceptInstaller();
}

void
Intercept::consume(const string &data, InterceptPlugin::RequestDataType type)
{
  if (type == InterceptPlugin::REQUEST_HEADER) {
    cout << "Read request header data" << endl << data;
  } else {
    cout << "Read request body data" << endl << data << endl;
  }
}

void
Intercept::handleInputComplete()
{
  cout << "Request data complete" << endl;
  string response("HTTP/1.1 200 OK\r\n"
                  "Content-Length: 7\r\n"
                  "\r\n");
  InterceptPlugin::produce(response);
  //  sleep(5); TODO: this is a test for streaming; currently doesn't work
  response = "hello\r\n";
  InterceptPlugin::produce(response);
  InterceptPlugin::setOutputComplete();
}
