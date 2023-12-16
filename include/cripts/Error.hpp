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

#include "ts/ts.h"
#include "ts/remap.h"

#include <utility>

namespace Cript
{
class Context;
}

class Error
{
public:
  class Message
  {
    using self_type = Message;

  public:
    Message()                       = default;
    Message(const Message &)        = delete;
    void operator=(const Message &) = delete;

    static void _set(Cript::Context *context, const Cript::string_view msg);

    [[nodiscard]] Cript::StringViewWrapper
    message() const
    {
      return {_message.c_str(), _message.size()};
    }

  private:
    void
    setter(const Cript::string_view msg)
    {
      _message = msg;
    }

    Cript::string _message;
  };

#undef Status
#undef Error
  class Status
  {
    using self_type = Status;

  public:
    Status()                       = default;
    Status(const Status &)         = delete;
    void operator=(const Status &) = delete;

    static void _set(Cript::Context *context, TSHttpStatus _status);

    static void
    _set(Cript::Context *context, int _status)
    {
      _set(context, static_cast<TSHttpStatus>(_status));
    }

    [[nodiscard]] TSHttpStatus
    status() const
    {
      return _status;
    }

  private:
    void
    setter(TSHttpStatus status)
    {
      _status = status;
    }

  private:
    TSHttpStatus _status = TS_HTTP_STATUS_NONE;
  }; // End class Status

public:
  Error() = default;

  [[nodiscard]] bool
  failed() const
  {
    return _failed;
  }

  void
  fail()
  {
    _failed = true;
  }

  // Check if we have an error, and set appropriate exit codes etc.
  void execute(Cript::Context *context);

private:
  Message _message;
  Status _status;
  bool _failed = false;
};
