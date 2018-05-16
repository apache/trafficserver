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

#include <sstream>
#include <iostream>
#include <string_view>
#include <atscppapi/PluginInit.h>
#include <atscppapi/GlobalPlugin.h>
#include <atscppapi/TransformationPlugin.h>
#include <atscppapi/Logger.h>

#include <Magick++.h>

using std::string;
using namespace Magick;
using namespace atscppapi;

#define TAG "webp_transform"

class ImageTransform : public TransformationPlugin
{
public:
  ImageTransform(Transaction &transaction) : TransformationPlugin(transaction, TransformationPlugin::RESPONSE_TRANSFORMATION)
  {
    TransformationPlugin::registerHook(HOOK_READ_RESPONSE_HEADERS);
  }

  void
  handleReadResponseHeaders(Transaction &transaction) override
  {
    transaction.getServerResponse().getHeaders()["Content-Type"] = "image/webp";
    transaction.getServerResponse().getHeaders()["Vary"]         = "Content-Type"; // to have a separate cache entry.

    TS_DEBUG(TAG, "url %s", transaction.getServerRequest().getUrl().getUrlString().c_str());
    transaction.resume();
  }

  void
  consume(std::string_view data) override
  {
    _img.write(data.data(), data.length());
  }

  void
  handleInputComplete() override
  {
    string input_data = _img.str();
    Blob input_blob(input_data.data(), input_data.length());
    Image image;
    image.read(input_blob);

    Blob output_blob;
    image.magick("WEBP");
    image.write(&output_blob);
    produce(std::string_view(reinterpret_cast<const char *>(output_blob.data()), output_blob.length()));

    setOutputComplete();
  }

  ~ImageTransform() override {}

private:
  std::stringstream _img;
};

class GlobalHookPlugin : public GlobalPlugin
{
public:
  GlobalHookPlugin() { registerHook(HOOK_READ_RESPONSE_HEADERS); }
  void
  handleReadResponseHeaders(Transaction &transaction) override
  {
    string ctype      = transaction.getServerResponse().getHeaders().values("Content-Type");
    string user_agent = transaction.getServerRequest().getHeaders().values("User-Agent");
    if (user_agent.find("Chrome") != string::npos && (ctype.find("jpeg") != string::npos || ctype.find("png") != string::npos)) {
      TS_DEBUG(TAG, "Content type is either jpeg or png. Converting to webp");
      transaction.addPlugin(new ImageTransform(transaction));
    }

    transaction.resume();
  }
};

void
TSPluginInit(int argc ATSCPPAPI_UNUSED, const char *argv[] ATSCPPAPI_UNUSED)
{
  RegisterGlobalPlugin("CPP_Webp_Transform", "apache", "dev@trafficserver.apache.org");
  InitializeMagick("");
  new GlobalHookPlugin();
}
