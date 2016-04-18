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
class HelloWorldPlugin : public atscppapi::GlobalPlugin
{
public:
  HelloWorldPlugin() { std::cout << "Hello World!" << std::endl; }
};

void
TSPluginInit(int argc ATSCPPAPI_UNUSED, const char *argv[] ATSCPPAPI_UNUSED)
{
  std::cout << "Hello from " << argv[0] << std::endl;
  atscppapi::RegisterGlobalPlugin("CPP_Example_HelloWorld", "apache", "dev@trafficserver.apache.org");
  new HelloWorldPlugin();
}
