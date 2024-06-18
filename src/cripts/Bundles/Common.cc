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

#include "cripts/Lulu.hpp"
#include "cripts/Preamble.hpp"
#include "cripts/Bundles/Common.hpp"

namespace Bundle
{
const Cript::string Common::_name = "Bundle::Common";

bool
Common::validate(std::vector<Cript::Bundle::Error> &errors) const
{
  bool good = true;

  // The .dscp() can only be 0 - 63
  if (_dscp < 0 || _dscp > 63) {
    errors.emplace_back("dscp must be between 0 and 63", name(), "dscp");
    good = false;
  }

  return good;
}

void
Common::doReadResponse(Cript::Context *context)
{
  borrow resp = Server::Response::get();

  // .cache_control(str)
  if (!_cc.empty() && (resp.status > 199) && (resp.status < 400) && (resp["Cache-Control"].empty() || _force_cc)) {
    resp["Cache-Control"] = _cc;
  }
}

void
Common::doRemap(Cript::Context *context)
{
  borrow conn = Client::Connection::get();

  // .dscp(int)
  if (_dscp > 0) {
    CDebug("Setting DSCP = {}", _dscp);
    conn.dscp = _dscp;
  }
}

} // namespace Bundle
