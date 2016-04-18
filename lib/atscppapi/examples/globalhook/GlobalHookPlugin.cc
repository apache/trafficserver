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
//#include<../ts/Diags.h>

using namespace atscppapi;
using namespace std;

class GlobalHookPlugin : public GlobalPlugin
{
public:
  GlobalHookPlugin() { registerHook(HOOK_READ_REQUEST_HEADERS_PRE_REMAP); }
  virtual void
  handleReadRequestHeadersPreRemap(Transaction &transaction)
  {
    std::cout << "Hello from handleReadRequesHeadersPreRemap!" << std::endl;
    transaction.resume();
  }
};

void
TSPluginInit(int argc ATSCPPAPI_UNUSED, const char *argv[] ATSCPPAPI_UNUSED)
{
  RegisterGlobalPlugin("CPP_Example_GlobalHookPplugin", "apache", "dev@trafficserver.apache.org");
  std::cout << "Hello from " << argv[0] << std::endl;
  new GlobalHookPlugin();
}
