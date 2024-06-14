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

namespace Cript
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

    Error(const Cript::string &message, Cript::string_view bundle, Cript::string_view option)
      : _message(message), _bundle(bundle), _option(option)
    {
    }

    [[nodiscard]] Cript::string_view
    message() const
    {
      return {_message};
    }

    [[nodiscard]] Cript::string_view
    bundle() const
    {
      return {_bundle};
    }

    [[nodiscard]] Cript::string_view
    option() const
    {
      return {_option};
    }

  private:
    std::string        _message;
    Cript::string_view _bundle;
    Cript::string_view _option;
  };

  class Base
  {
    using self_type = Base;

  public:
    Base()                            = default;
    Base(const self_type &)           = delete;
    void operator=(const self_type &) = delete;
    virtual ~Base()                   = default;

    virtual const Cript::string &name() const = 0;

    void
    needCallback(Cript::Callbacks cb)
    {
      _callbacks |= cb;
    }

    void
    needCallback(unsigned cbs)
    {
      _callbacks |= cbs;
    }

    void
    needCallback(std::initializer_list<unsigned> cb_list)
    {
      for (auto &it : cb_list) {
        _callbacks |= it;
      }
    }

    [[nodiscard]] unsigned
    callbacks() const
    {
      return _callbacks;
    }

    virtual bool
    validate(std::vector<Cript::Bundle::Error> &errors) const
    {
      return true;
    }

    virtual void
    doRemap(Cript::Context *context)
    {
    }

    virtual void
    doPostRemap(Cript::Context *context)
    {
    }

    virtual void
    doSendResponse(Cript::Context *context)
    {
    }

    virtual void
    doCacheLookup(Cript::Context *context)
    {
    }

    virtual void
    doSendRequest(Cript::Context *context)
    {
    }

    virtual void
    doReadResponse(Cript::Context *context)
    {
    }

    virtual void
    doTxnClose(Cript::Context *context)
    {
    }

  protected:
    unsigned _callbacks = 0;
  }; // Class Base

} // namespace Bundle

} // namespace Cript
