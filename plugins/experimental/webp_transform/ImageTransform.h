/** @file

    ATSCPPAPI plugin to do webp transform.

    @section license License

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

#ifndef IMAGETRANSFROM_H_
#define IMAGETRANSFROM_H_

#include <string>
#include <atscppapi/GlobalPlugin.h>
#include <atscppapi/TransactionPlugin.h>
#include <atscppapi/TransformationPlugin.h>
#include <atscppapi/Transaction.h>


class ImageTransform : public atscppapi::TransformationPlugin
{
public:
  ImageTransform(atscppapi::Transaction &transaction);

  void handleReadResponseHeaders(atscppapi::Transaction &transaction);
  void consume(const std::string &data);
  void handleInputComplete();
  virtual ~ImageTransform();

  static std::string FIELD_USER_AGENT;
  static std::string FIELD_TRANSFORM_IMAGE;
  static std::string CONTEXT_IMG_TRANSFORM;
  static std::string USER_AGENT_CHROME;
  static std::string FIELD_CONTENT_TYPE;
  static std::string FIELD_VARY;
  static std::string IMAGE_TYPE;

private:
  std::stringstream _img;
  WebpTransform _webp_transform;
};

class GlobalHookPlugin : public atscppapi::GlobalPlugin
{
public:
  GlobalHookPlugin();
  virtual void handleReadResponseHeaders(atscppapi::Transaction &transaction);
};


#endif /* IMAGETRANSFROM_H_ */
