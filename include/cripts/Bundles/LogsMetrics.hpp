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

// This is a bundle for some per-remap metrics and logging.
//
//  Bundle::LogsMetrics::activate().propstats("property-name")
//                                 .logsample(2000)
//                                 .tcpinfo();
//
#pragma once

#include "cripts/Bundle.hpp"

namespace Bundle
{
class LogsMetrics : public Cript::Bundle::Base
{
  using super_type = Cript::Bundle::Base;
  using self_type  = LogsMetrics;

public:
  LogsMetrics(Cript::Instance *inst) : _inst(inst) {}

  static self_type &
  _activate(Cript::Instance &inst)
  {
    auto *entry = new self_type(&inst);

    inst.addBundle(entry);

    return *entry;
  }

  const Cript::string &
  name() const override
  {
    return _name;
  }

  self_type &propstats(const Cript::string_view &label); // In LogsMetrics.cc

  self_type &
  logsample(int val)
  {
    needCallback(Cript::Callbacks::DO_REMAP);
    _log_sample = val;

    return *this;
  }

  self_type &
  tcpinfo(bool enable = true)
  {
    if (enable) {
      needCallback({Cript::Callbacks::DO_REMAP, Cript::Callbacks::DO_SEND_RESPONSE, Cript::Callbacks::DO_TXN_CLOSE});
    }
    _tcpinfo = enable;

    return *this;
  }

  void doCacheLookup(Cript::Context *context) override;
  void doSendResponse(Cript::Context *context) override;
  void doTxnClose(Cript::Context *context) override;
  void doRemap(Cript::Context *context) override;

private:
  static const Cript::string _name;
  Cript::Instance           *_inst;               // This Bundle needs the instance for access to the instance metrics
  Cript::string              _label      = "";    // Propstats label
  int                        _log_sample = 0;     // Log sampling
  bool                       _tcpinfo    = false; // Turn on TCP info logging
};

} // namespace Bundle
