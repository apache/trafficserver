/** @file

  @section license License

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

#include "plugin.h"
#include "header.h"

#include "ts/ts.h"

#include <string>
#include <string_view>

static void
put_header(TSHttpTxn txnp, const std::string &name, const std::string &value, bool overwrite)
{
  TSMBuffer bufp;
  TSMLoc    hdr_loc;
  if (TS_SUCCESS != TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc)) {
    Dbg(dbg_ctl, "Failed to get headers.");
    return;
  }

  TSMLoc target = TSMimeHdrFieldFind(bufp, hdr_loc, name.c_str(), name.length());
  if (target == TS_NULL_MLOC) {
    // Add - Create a new field with the value
    Dbg(dbg_ctl, "Add %s: %s", name.c_str(), value.c_str());
    TSMimeHdrFieldCreateNamed(bufp, hdr_loc, name.c_str(), name.length(), &target);
    TSMimeHdrFieldValueStringSet(bufp, hdr_loc, target, -1, value.c_str(), value.length());
    TSMimeHdrFieldAppend(bufp, hdr_loc, target);
    TSHandleMLocRelease(bufp, hdr_loc, target);
  } else if (overwrite) {
    // Replace - Set the value to the first field and remove all duplicate fields
    Dbg(dbg_ctl, "Replace %s field value with %s", name.c_str(), value.c_str());
    TSMLoc tmp   = nullptr;
    bool   first = true;
    while (target) {
      tmp = TSMimeHdrFieldNextDup(bufp, hdr_loc, target);
      if (first) {
        first = false;
        TSMimeHdrFieldValueStringSet(bufp, hdr_loc, target, -1, value.c_str(), value.size());
      } else {
        TSMimeHdrFieldDestroy(bufp, hdr_loc, target);
      }
      TSHandleMLocRelease(bufp, hdr_loc, target);
      target = tmp;
    }
  } else {
    // Append - Find the last duplicate field and set the value to it
    Dbg(dbg_ctl, "Append %s to %s field value", value.c_str(), name.c_str());
    TSMLoc dup = TSMimeHdrFieldNextDup(bufp, hdr_loc, target);
    while (dup != TS_NULL_MLOC) {
      TSHandleMLocRelease(bufp, hdr_loc, target);
      target = dup;
      dup    = TSMimeHdrFieldNextDup(bufp, hdr_loc, target);
    }
    TSMimeHdrFieldValueStringInsert(bufp, hdr_loc, target, -1, value.c_str(), value.length());
    TSHandleMLocRelease(bufp, hdr_loc, target);
  }

  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
}

static void
put_via_header(TSHttpTxn txnp, const std::string &via_header, bool overwrite)
{
  TSMgmtString proxy_name = nullptr;
  if (TS_SUCCESS == TSMgmtStringGet("proxy.config.proxy_name", &proxy_name)) {
    put_header(txnp, via_header, proxy_name, overwrite);
    TSfree(proxy_name);
  } else {
    TSError("[%s] Failed to get proxy name for %s, set 'proxy.config.proxy_name' in records.config", PLUGIN_NAME,
            via_header.c_str());
    put_header(txnp, via_header, "unknown", overwrite);
  }
}

void
set_header(TSHttpTxn txnp, const std::string &header, const std::string &fingerprint)
{
  put_header(txnp, header, fingerprint, true);
}

void
append_header(TSHttpTxn txnp, const std::string &header, const std::string &fingerprint)
{
  put_header(txnp, header, fingerprint, false);
}

void
set_via_header(TSHttpTxn txnp, const std::string &via_header)
{
  put_via_header(txnp, via_header, true);
}

void
append_via_header(TSHttpTxn txnp, const std::string &via_header)
{
  put_via_header(txnp, via_header, false);
}

void
remove_header(TSHttpTxn txnp, const std::string &header)
{
  TSMBuffer bufp;
  TSMLoc    hdr_loc;
  if (TS_SUCCESS != TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc)) {
    Dbg(dbg_ctl, "Failed to get headers.");
    return;
  }

  TSMLoc target = TSMimeHdrFieldFind(bufp, hdr_loc, header.c_str(), header.length());
  if (target != TS_NULL_MLOC) {
    // Remove all
    Dbg(dbg_ctl, "Remove all %s field", header.c_str());
    TSMLoc tmp = nullptr;
    while (target) {
      tmp = TSMimeHdrFieldNextDup(bufp, hdr_loc, target);
      TSMimeHdrFieldDestroy(bufp, hdr_loc, target);
      TSHandleMLocRelease(bufp, hdr_loc, target);
      target = tmp;
    }
  }
}
