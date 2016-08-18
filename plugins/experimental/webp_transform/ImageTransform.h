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
#include <atscppapi/GlobalPlugin.h>
#include <atscppapi/TransformationPlugin.h>
#include <atscppapi/Logger.h>

#include <Magick++.h>

using namespace std;
using std::string;
using namespace Magick;
using namespace atscppapi;

class ImageTransform : public TransformationPlugin
{
public:
  ImageTransform(Transaction &transaction) : TransformationPlugin(transaction, TransformationPlugin::RESPONSE_TRANSFORMATION)
  {
    TransformationPlugin::registerHook(HOOK_READ_RESPONSE_HEADERS);
  }

  void handleReadResponseHeaders(Transaction &transaction);
  void consume(const string &data);
  void handleInputComplete();
  Blob &
  getInputBlob()
  {
    return input_blob_;
  }
  Blob &
  getOutputBlob()
  {
    return output_blob_;
  }
  Image &
  getImageObject()
  {
    return image_;
  }

  virtual ~ImageTransform() {}
private:
  std::stringstream _img;
  Image image_;
  Blob input_blob_, output_blob_;
};

class GlobalHookPlugin : public GlobalPlugin
{
public:
  GlobalHookPlugin() { registerHook(HOOK_READ_RESPONSE_HEADERS); }
  virtual void handleReadResponseHeaders(Transaction &transaction);
};
