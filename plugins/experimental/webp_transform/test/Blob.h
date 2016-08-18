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


#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <string>

namespace Magick
{
class Blob
{
public:
  Blob() {}
  Blob(const void *data_, const size_t length_) {}
  Blob(const Blob &blob_) {}
  MOCK_METHOD1(assign, Blob &(const Blob &));
  Blob &
  operator=(const Blob &blob_)
  {
    return assign(blob_);
  }

  MOCK_CONST_METHOD1(compare, bool(const Blob &));
  bool
  operator==(const Blob &rhs) const
  {
    return compare(rhs);
  }

  MOCK_CONST_METHOD0(data, const void *());
  MOCK_CONST_METHOD0(length, size_t());
  MOCK_METHOD2(update, void(const void *data, const size_t length));
};
}
