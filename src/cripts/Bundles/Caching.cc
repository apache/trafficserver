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

#include "cripts/Preamble.hpp"
#include "cripts/Bundles/Caching.hpp"

namespace cripts::Bundle
{
const cripts::string Caching::_name = "Bundle::Caching";

void
Caching::doRemap(cripts::Context *context)
{
  // .disable(bool)
  if (_disabled) {
    proxy.config.http.cache.http.Set(0);
    CDebug("Caching disabled");
  }
}

void
Caching::doReadResponse(cripts::Context *context)
{
  borrow resp = cripts::Server::Response::Get();

  // .cache_control(str)
  if (!_cc.empty() && (resp.status > 199) && (resp.status < 400) && (resp["Cache-Control"].empty() || _force_cc)) {
    resp["Cache-Control"] = _cc;
  }
}

} // namespace cripts::Bundle
