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

// This is a bundle for some per-remap metrics and logging.
//  Bundle::LogsMetrics::activate().propstats("property-name")
//                                 .logsample(2000)
//                                 .tcpinfo();

#include "cripts/Lulu.hpp"
#include "cripts/Instance.hpp"
#include "cripts/Bundle.hpp"

namespace cripts::Bundle
{
class LogsMetrics : public cripts::Bundle::Base
{
  using super_type = cripts::Bundle::Base;
  using self_type  = LogsMetrics;

public:
  LogsMetrics(cripts::Instance *inst) : _inst(inst) {}

  static self_type &
  _activate(cripts::Instance &inst)
  {
    auto *entry = new self_type(&inst);

    inst.AddBundle(entry);

    return *entry;
  }

  [[nodiscard]] const cripts::string &
  Name() const override
  {
    return _name;
  }

  self_type &propstats(const cripts::string_view &label); // In LogsMetrics.cc

  self_type &
  logsample(int val)
  {
    NeedCallback(cripts::Callbacks::DO_REMAP);
    _log_sample = val;

    return *this;
  }

  self_type &
  tcpinfo(bool enable = true)
  {
    if (enable) {
      NeedCallback({cripts::Callbacks::DO_REMAP, cripts::Callbacks::DO_SEND_RESPONSE, cripts::Callbacks::DO_TXN_CLOSE});
    }
    _tcpinfo = enable;

    return *this;
  }

  void doCacheLookup(cripts::Context *context) override;
  void doSendResponse(cripts::Context *context) override;
  void doTxnClose(cripts::Context *context) override;
  void doRemap(cripts::Context *context) override;

private:
  static const cripts::string _name;
  cripts::Instance           *_inst;               // This Bundle needs the instance for access to the instance metrics
  cripts::string              _label      = "";    // Propstats label
  int                         _log_sample = 0;     // Log sampling
  bool                        _tcpinfo    = false; // Turn on TCP info logging
};

} // namespace cripts::Bundle
