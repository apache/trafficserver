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

// This is an example bundle for some common tasks.
//
//  Bundle::Common::activate().dscp(10)
//                            .cache_control("max-age=259200");
#pragma once

#include "cripts/Bundle.hpp"

namespace Bundle
{
class Common : public Cript::Bundle::Base
{
  using super_type = Cript::Bundle::Base;
  using self_type  = Common;

public:
  using super_type::Base;

  bool validate(std::vector<Cript::Bundle::Error> &errors) const override;

  // This is the factory to create an instance of this bundle
  static self_type &
  _activate(Cript::Instance &inst)
  {
    auto *entry = new self_type();

    inst.bundles.push_back(entry);

    return *entry;
  }

  self_type &
  dscp(int val)
  {
    needCallback(Cript::Callbacks::DO_REMAP);
    _dscp = val;

    return *this;
  }

  self_type &
  cache_control(Cript::string_view cc, bool force = false)
  {
    needCallback(Cript::Callbacks::DO_READ_RESPONSE);
    _cc       = cc;
    _force_cc = force;

    return *this;
  }

  void doReadResponse(Cript::Context *context) override;
  void doRemap(Cript::Context *context) override;

private:
  Cript::string _cc = "";
  int _dscp         = 0;
  bool _force_cc    = false;
};

} // namespace Bundle
