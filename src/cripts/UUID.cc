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

namespace cripts
{

cripts::string
UUID::Unique::_get(cripts::Context *context)
{
  cripts::string ret;
  char           uuid[TS_CRUUID_STRING_LEN + 1];

  if (TS_SUCCESS == TSClientRequestUuidGet(context->state.txnp, uuid)) {
    ret = uuid;
  }
  return ret; // RVO
}

cripts::string
UUID::Request::_get(cripts::Context *context)
{
  uint64_t       uuid = TSHttpTxnIdGet(context->state.txnp);
  cripts::string ret  = std::to_string(uuid);

  return ret;
}

} // namespace cripts
