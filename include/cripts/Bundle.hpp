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

#include <vector>

#include "cripts/Lulu.hpp"
#include "cripts/Transaction.hpp"

namespace cripts
{
// We have to forward declare this, to avoid some circular dependencies
class Context;

namespace Bundle
{
  class Error
  {
    using self_type = Error;

  public:
    Error()                           = delete;
    void operator=(const self_type &) = delete;
    virtual ~Error()                  = default;

    Error(const cripts::string &message, cripts::string_view bundle, cripts::string_view option)
      : _message(message), _bundle(bundle), _option(option)
    {
    }

    [[nodiscard]] cripts::string_view
    Message() const
    {
      return {_message};
    }

    [[nodiscard]] cripts::string_view
    Bundle() const
    {
      return {_bundle};
    }

    [[nodiscard]] cripts::string_view
    Option() const
    {
      return {_option};
    }

  private:
    std::string         _message;
    cripts::string_view _bundle;
    cripts::string_view _option;
  };

  class Base
  {
    using self_type = Base;

  public:
    Base()                            = default;
    Base(const self_type &)           = delete;
    void operator=(const self_type &) = delete;
    virtual ~Base()                   = default;

    [[nodiscard]] virtual const cripts::string &Name() const = 0;

    void
    NeedCallback(cripts::Callbacks cb)
    {
      _callbacks |= cb;
    }

    void
    NeedCallback(unsigned cbs)
    {
      _callbacks |= cbs;
    }

    void
    NeedCallback(std::initializer_list<unsigned> cb_list)
    {
      for (auto &it : cb_list) {
        _callbacks |= it;
      }
    }

    [[nodiscard]] unsigned
    Callbacks() const
    {
      return _callbacks;
    }

    virtual bool
    Validate(std::vector<cripts::Bundle::Error> & /* errors ATS_UNUSED */) const
    {
      return true;
    }

    virtual void
    doRemap(cripts::Context * /* context ATS_UNUSED */)
    {
    }

    virtual void
    doPostRemap(cripts::Context * /* context ATS_UNUSED */)
    {
    }

    virtual void
    doSendResponse(cripts::Context * /* context ATS_UNUSED */)
    {
    }

    virtual void
    doCacheLookup(cripts::Context * /* context ATS_UNUSED */)
    {
    }

    virtual void
    doSendRequest(cripts::Context * /* context ATS_UNUSED */)
    {
    }

    virtual void
    doReadResponse(cripts::Context * /* context ATS_UNUSED */)
    {
    }

    virtual void
    doTxnClose(cripts::Context * /* context ATS_UNUSED */)
    {
    }

  protected:
    uint32_t _callbacks = 0;
  }; // Class Base

} // namespace Bundle

} // namespace cripts
