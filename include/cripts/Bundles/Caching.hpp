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

//  Cache specific features:
//  Bundle::Caching::activate().cache_control("max-age=259200")
//                             .disable(true)

#include "cripts/Lulu.hpp"
#include "cripts/Instance.hpp"
#include "cripts/Bundle.hpp"

namespace cripts::Bundle
{
class Caching : public cripts::Bundle::Base
{
  using super_type = cripts::Bundle::Base;
  using self_type  = Caching;

public:
  using super_type::Base;

  // This is the factory to create an instance of this bundle
  static self_type &
  _activate(cripts::Instance &inst)
  {
    auto *entry = new self_type();

    inst.AddBundle(entry);

    return *entry;
  }

  [[nodiscard]] const cripts::string &
  Name() const override
  {
    return _name;
  }

  self_type &
  cache_control(cripts::string_view cc, bool force = false)
  {
    NeedCallback(cripts::Callbacks::DO_READ_RESPONSE);
    _cc       = cc;
    _force_cc = force;

    return *this;
  }

  self_type &
  disable(bool disable = true)
  {
    NeedCallback(cripts::Callbacks::DO_REMAP);
    _disabled = disable;

    return *this;
  }

  void doReadResponse(cripts::Context *context) override;
  void doRemap(cripts::Context *context) override;

private:
  static const cripts::string _name;
  cripts::string              _cc       = "";
  bool                        _force_cc = false;
  bool                        _disabled = false;
};

} // namespace cripts::Bundle
