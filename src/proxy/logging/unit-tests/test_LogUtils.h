/** @file

  Header file for shared declarations/definitions for test_LogUtils.cc and LogUtils.h for unit testing.

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

#pragma once

#include <cstring>
#include <swoc/MemSpan.h>

struct MIMEField {
  const char *tag, *value;

  std::string_view
  name_get() const
  {
    return {tag};
  }
  std::string_view
  value_get() const
  {
    return {value};
  }
};

class MIMEHdr
{
private:
  swoc::MemSpan<MIMEField const> _fields;

public:
  MIMEHdr(const MIMEField *first, int count) : _fields{first, size_t(count)} {}

  auto
  begin()
  {
    return _fields.begin();
  }
  auto
  end()
  {
    return _fields.end();
  }
};
