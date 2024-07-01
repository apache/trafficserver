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

#include <utility>

#include "ts/ts.h"
#include "ts/remap.h"

#include "cripts/Lulu.hpp"

namespace Cript
{
class Context;
}

class Error
{
public:
  class Reason
  {
    using self_type = Reason;

  public:
    Reason()                          = default;
    Reason(const self_type &)         = delete;
    void operator=(const self_type &) = delete;

    static void _set(Cript::Context *context, const Cript::string_view msg);

  private:
    friend class Error;

    [[nodiscard]] Cript::string_view
    _getter() const
    {
      return {_reason.c_str(), _reason.size()};
    }

    void
    _setter(const Cript::string_view msg)
    {
      _reason = msg;
    }

    Cript::string _reason;
  };

#undef Status
#undef Error
  class Status
  {
    using self_type = Status;

  public:
    Status()                          = default;
    Status(const self_type &)         = delete;
    void operator=(const self_type &) = delete;

    static void _set(Cript::Context *context, TSHttpStatus _status);

    static void
    _set(Cript::Context *context, int _status)
    {
      _set(context, static_cast<TSHttpStatus>(_status));
    }

    static TSHttpStatus _get(Cript::Context *context);

  private:
    friend class Error;

    [[nodiscard]] TSHttpStatus
    _getter() const
    {
      return _status;
    }

    void
    _setter(TSHttpStatus status)
    {
      _status = status;
    }

  private:
    TSHttpStatus _status = TS_HTTP_STATUS_NONE;
  }; // End class Status

public:
  Error() = default;

  [[nodiscard]] bool
  Failed() const
  {
    return _failed;
  }

  void
  Fail()
  {
    _failed = true;
  }

  // Check if we have an error, and set appropriate exit codes etc.
  void Execute(Cript::Context *context);

private:
  Reason _reason;
  Status _status;
  bool   _failed = false;
};
