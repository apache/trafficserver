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

#include <string>
#include "Blob.h"

namespace Magick
{
class Blob;
class Image
{
public:
  Image() {}
  Image(const Blob &blob_) {}
  Image(const Image &image_) {}
  MOCK_METHOD1(assign, Image &(const Image &));
  Image &
  operator=(const Image &image_)
  {
    return assign(image_);
  }

  MOCK_METHOD1(read, void(const Blob &));
  MOCK_METHOD1(magick, void(const std::string &));
  MOCK_METHOD1(write, void(Blob *));
};
}
