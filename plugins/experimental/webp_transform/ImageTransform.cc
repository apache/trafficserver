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
#include <atscppapi/PluginInit.h>
#include <atscppapi/Logger.h>

#include "compress.h"
#include "Common.h"
#include "ImageTransform.h"

using namespace atscppapi;
using std::string;


std::string ImageTransform::FIELD_USER_AGENT("User-Agent");
std::string ImageTransform::FIELD_CONTENT_TYPE("Content-Type");
std::string ImageTransform::FIELD_TRANSFORM_IMAGE("@X-Transform-Image");
std::string ImageTransform::CONTEXT_IMG_TRANSFORM("Transform-Image");
std::string ImageTransform::USER_AGENT_CHROME("Chrome");
std::string ImageTransform::FIELD_VARY("Vary");
std::string ImageTransform::IMAGE_TYPE("image/webp");


ImageTransform::ImageTransform(Transaction &transaction)
  : TransformationPlugin(transaction, TransformationPlugin::RESPONSE_TRANSFORMATION), _webp_transform()
{
  TransformationPlugin::registerHook(HOOK_READ_RESPONSE_HEADERS);
}

ImageTransform::~ImageTransform()
{
  _webp_transform.finalize();
}

void
ImageTransform::handleReadResponseHeaders(Transaction &transaction)
{
  transaction.getClientResponse().getHeaders()[ImageTransform::FIELD_CONTENT_TYPE] = ImageTransform::IMAGE_TYPE;
  transaction.getClientResponse().getHeaders()[ImageTransform::FIELD_VARY] = ImageTransform::ImageTransform::FIELD_CONTENT_TYPE;

  TS_DEBUG(TAG, "Image Transformation Plugin for url %s", transaction.getServerRequest().getUrl().getUrlString().c_str());
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
  _webp_transform.init();
  _webp_transform.transform(_img);
  produce(_webp_transform.getTransformedImage().str());

  setOutputComplete();
}

GlobalHookPlugin::GlobalHookPlugin()
{
  registerHook(HOOK_READ_RESPONSE_HEADERS);
}

void
GlobalHookPlugin::handleReadResponseHeaders(Transaction &transaction)
{
  // add transformation only for jpeg files
  string ctype = transaction.getServerResponse().getHeaders().values(ImageTransform::FIELD_CONTENT_TYPE);
  string user_agent = transaction.getServerRequest().getHeaders().values(ImageTransform::FIELD_USER_AGENT);
  if (user_agent.find(ImageTransform::USER_AGENT_CHROME) != string::npos &&
      (ctype.find("jpeg") != string::npos || ctype.find("png") != string::npos)) {
    transaction.addPlugin(new ImageTransform(transaction));
  }
  transaction.resume();
}

void
TSPluginInit(int argc ATSCPPAPI_UNUSED, const char *argv[] ATSCPPAPI_UNUSED)
{
  RegisterGlobalPlugin("CPP_Webp_Transform", "apache", "dev@trafficserver.apache.org");
  new GlobalHookPlugin();
}
