/** @file

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

#include <string_view>

class Datasource
{
public:
  Datasource()                                                     = default;
  virtual ~Datasource()                                            = default;
  virtual std::string_view get_method()                            = 0;
  virtual int              get_version()                           = 0;
  virtual bool             has_cookie_field()                      = 0;
  virtual bool             has_referer_field()                     = 0;
  virtual int              get_field_count()                       = 0;
  virtual std::string_view get_accept_language()                   = 0;
  virtual void             get_headers_hash(unsigned char out[32]) = 0;

protected:
  bool _should_include_field(std::string_view name);
};
