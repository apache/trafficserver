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

#include <atscppapi/RemapPlugin.h>
#include <atscppapi/PluginInit.h>
#include <string>

using namespace std;
using namespace atscppapi;

class MyRemapPlugin : public RemapPlugin
{
public:
  MyRemapPlugin(void **instance_handle) : RemapPlugin(instance_handle) {}
  Result
  doRemap(const Url &map_from_url, const Url &map_to_url, Transaction &transaction, bool &redirect)
  {
    if (transaction.getClientRequest().getUrl().getQuery().find("custom=1") != string::npos) {
      transaction.setStatusCode(HTTP_STATUS_FORBIDDEN);
      if (transaction.getClientRequest().getUrl().getQuery().find("output=xml") != string::npos) {
        transaction.setErrorBody(
          "<Error>Hello! This is a custom response without making an origin request and no server intercept.</Error>",
          "application/xml");
      } else {
        transaction.setErrorBody("Hello! This is a custom response without making an origin request and no server intercept.");
      }

      return RESULT_DID_REMAP;
    }

    return RESULT_NO_REMAP;
  }
};

TSReturnCode
TSRemapNewInstance(int argc ATSCPPAPI_UNUSED, char *argv[] ATSCPPAPI_UNUSED, void **instance_handle, char *errbuf ATSCPPAPI_UNUSED,
                   int errbuf_size ATSCPPAPI_UNUSED)
{
  new MyRemapPlugin(instance_handle);
  return TS_SUCCESS;
}
