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

#include "ImageTransform.h"

namespace
{
#define TAG "webp_transform"
}

void
ImageTransform::handleReadResponseHeaders(Transaction &transaction)
{
  transaction.getServerResponse().getHeaders()["Content-Type"] = "image/webp";
  transaction.getServerResponse().getHeaders()["Vary"]         = "Content-Type"; // to have a separate cache entry.

  TS_DEBUG(TAG, "url %s", transaction.getServerRequest().getUrl().getUrlString().c_str());
  transaction.resume();
}

void
ImageTransform::consume(const string &data)
{
  _img.write(data.data(), data.size());
}

void
ImageTransform::handleInputComplete()
{
  string input_data = _img.str();
  input_blob_.update(input_data.data(), input_data.length());
  image_.read(input_blob_);

  image_.magick("WEBP");
  image_.write(&output_blob_);
  string output_data(reinterpret_cast<const char *>(output_blob_.data()), output_blob_.length());
  produce(output_data);

  setOutputComplete();
}

void
GlobalHookPlugin::handleReadResponseHeaders(Transaction &transaction)
{
  string ctype      = transaction.getServerResponse().getHeaders().values("Content-Type");
  string user_agent = transaction.getServerRequest().getHeaders().values("User-Agent");
  if (user_agent.find("Chrome") != string::npos && (ctype.find("jpeg") != string::npos || ctype.find("png") != string::npos)) {
    TS_DEBUG(TAG, "Content type is either jpeg or png. Converting to webp");
    transaction.addPlugin(new ImageTransform(transaction));
  }

  transaction.resume();
}

void
TSPluginInit(int argc ATSCPPAPI_UNUSED, const char *argv[] ATSCPPAPI_UNUSED)
{
  RegisterGlobalPlugin("CPP_Webp_Transform", "apache", "dev@trafficserver.apache.org");
  InitializeMagick("");
  new GlobalHookPlugin();
}
