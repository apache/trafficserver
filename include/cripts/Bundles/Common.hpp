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

// This is an example bundle for some common tasks.
//  Bundle::Common::activate().dscp(10)
//                            .via_header("Client|Origin", "disable|protocol|basic|detailed|full")
//                            .set_config("config", value);

#include "cripts/Lulu.hpp"
#include "cripts/Instance.hpp"
#include "cripts/Bundle.hpp"
#include <cripts/ConfigsBase.hpp>

namespace cripts::Bundle
{
class Common : public cripts::Bundle::Base
{
  using super_type = cripts::Bundle::Base;
  using self_type  = Common;

  using RecordsList = std::vector<std::pair<const cripts::Records *, const cripts::Records::ValueType>>;

public:
  using super_type::Base;

  bool Validate(std::vector<cripts::Bundle::Error> &errors) const override;

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
  dscp(int val)
  {
    NeedCallback(cripts::Callbacks::DO_REMAP);
    _dscp = val;

    return *this;
  }

  self_type &via_header(const cripts::string_view &destination, const cripts::string_view &value);
  self_type &set_config(const cripts::string_view name, const cripts::Records::ValueType &value);
  self_type &set_config(const std::vector<std::pair<const cripts::string_view, const cripts::Records::ValueType>> &configs);

  void doRemap(cripts::Context *context) override;

private:
  static const cripts::string _name;
  int                         _dscp       = 0;
  std::pair<int, bool>        _client_via = {0, false}; // Flag indicates if it's been set at all
  std::pair<int, bool>        _origin_via = {0, false};
  RecordsList                 _configs;
};

} // namespace cripts::Bundle
