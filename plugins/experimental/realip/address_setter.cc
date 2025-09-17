/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "realip.h"
#include "address_setter.h"

AddressSource *AddressSetter::source = nullptr;

int
AddressSetter::event_handler(TSCont /* contp ATS_UNUSED */, TSEvent event, void *edata)
{
  TSReleaseAssert(event == TS_EVENT_HTTP_READ_REQUEST_HDR);
  TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);

  if (source->verify(txnp)) {
    struct sockaddr_storage addr_storage;
    if (struct sockaddr *addr = source->get_address(txnp, &addr_storage); addr != nullptr) {
      TSHttpTxnVerifiedAddrSet(txnp, addr);
    } else {
      Dbg(dbg_ctl, "Failed to get client's IP address");
    }
  } else {
    Dbg(dbg_ctl, "Failed to verify the IP address source");
  }

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);

  return TS_EVENT_NONE;
}
