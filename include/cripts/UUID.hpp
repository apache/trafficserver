/*
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

#include "cripts/Lulu.hpp"
#include "ts/ts.h"
#include "ts/remap.h"

namespace Cript
{
class Context;

namespace UUID
{
  class Process
  {
    using self_type = Process;

  public:
    Process()                         = delete;
    Process(const self_type &)        = delete;
    void operator=(const self_type &) = delete;

    // This doesn't use the context so we can implement it here
    static Cript::string
    _get(Cript::Context * /* context ATS_UNUSED */)
    {
      TSUuid process = TSProcessUuidGet();

      return TSUuidStringGet(process);
    }

  }; // End class UUID::Process

  class Unique
  {
    using self_type = Unique;

  public:
    Unique()                          = delete;
    Unique(const self_type &)         = delete;
    void operator=(const self_type &) = delete;

    static Cript::string _get(Cript::Context *context);

  }; // End class UUID::Unique

  class Request
  {
    using self_type = Request;

  public:
    Request()                         = delete;
    Request(const self_type &)        = delete;
    void operator=(const self_type &) = delete;

    static Cript::string _get(Cript::Context *context);

  }; // End class UUID::Request

} // namespace UUID

} // namespace Cript
