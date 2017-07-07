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

//////////////////////////////////////////////////////////////////////////////////////////////
// expander.cc: Implementation of the Variable Expander base class
//
#include "ts/ts.h"

#include <string>
#include <sstream>

#include "lulu.h"
#include "statement.h"
#include "parser.h"
#include "expander.h"
#include "conditions.h"

// Main expander method
std::string
VariableExpander::expand(const Resources &res)
{
  std::string result;

  result.reserve(512); // TODO: Can be optimized
  result.assign(_source);

  while (true) {
    std::string::size_type start = result.find("%<");
    if (start == std::string::npos) {
      break;
    }

    std::string::size_type end = result.find('>', start);
    if (end == std::string::npos) {
      break;
    }

    std::string first_part = result.substr(0, start);
    std::string last_part  = result.substr(end + 1);

    // Now evaluate the variable
    std::string variable = result.substr(start, end - start + 1);

    // This will be the value to replace the "variable" section of the string with
    std::string resolved_variable = "";

    // Initialize some stuff
    TSMBuffer bufp;
    TSMLoc hdr_loc;
    TSMLoc url_loc;

    if (variable == "%<proto>") {
      // Protocol of the incoming request
      if (TSHttpTxnPristineUrlGet(res.txnp, &bufp, &url_loc) == TS_SUCCESS) {
        int len;
        const char *tmp = TSUrlSchemeGet(bufp, url_loc, &len);
        if ((tmp != nullptr) && (len > 0)) {
          resolved_variable.assign(tmp, len);
        } else {
          resolved_variable.assign("");
        }
        TSHandleMLocRelease(bufp, TS_NULL_MLOC, url_loc);
      }
    } else if (variable == "%<port>") {
      // Original port of the incoming request
      if (TSHttpTxnClientReqGet(res.txnp, &bufp, &hdr_loc) == TS_SUCCESS) {
        if (TSHttpHdrUrlGet(bufp, hdr_loc, &url_loc) == TS_SUCCESS) {
          std::stringstream out;
          out << TSUrlPortGet(bufp, url_loc);
          resolved_variable = out.str();
          TSHandleMLocRelease(bufp, hdr_loc, url_loc);
        }
        TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
      }
    } else if (variable == "%<chi>") {
      // IP address of the client's host machine
      resolved_variable = getIP(TSHttpTxnClientAddrGet(res.txnp));
    } else if (variable == "%<cqhl>") {
      // The client request header length; the header length in the client request to Traffic Server.
      std::stringstream out;
      out << TSHttpHdrLengthGet(res.client_bufp, res.client_hdr_loc);
      resolved_variable = out.str();
    } else if (variable == "%<cqhm>") {
      // The HTTP method in the client request to Traffic Server: GET, POST, and so on (subset of cqtx).
      int method_len;
      const char *methodp = TSHttpHdrMethodGet(res.client_bufp, res.client_hdr_loc, &method_len);
      if (methodp && method_len) {
        resolved_variable.assign(methodp, method_len);
      }
    } else if (variable == "%<cquup>") {
      // The client request unmapped URL path. This field records a URL path
      // before it is remapped (reverse proxy mode).
      if (TSHttpTxnPristineUrlGet(res.txnp, &bufp, &url_loc) == TS_SUCCESS) {
        int path_len;
        const char *path = TSUrlPathGet(bufp, url_loc, &path_len);

        if (path && path_len) {
          resolved_variable.assign(path, path_len);
        }
        TSHandleMLocRelease(bufp, TS_NULL_MLOC, url_loc);
      }
    } else if (variable == "%<cque>") {
      // The client request effective URL.
      int url_len = 0;
      char *url   = TSHttpTxnEffectiveUrlStringGet(res.txnp, &url_len);
      if (url && url_len) {
        resolved_variable.assign(url, url_len);
      }
      free(url);
      url = nullptr;
    } else if (variable == "%<INBOUND:REMOTE-ADDR>") {
      ConditionInbound::append_value(resolved_variable, res, NET_QUAL_REMOTE_ADDR);
    } else if (variable == "%<INBOUND:REMOTE-PORT>") {
      ConditionInbound::append_value(resolved_variable, res, NET_QUAL_REMOTE_PORT);
    } else if (variable == "%<INBOUND:LOCAL-ADDR>") {
      ConditionInbound::append_value(resolved_variable, res, NET_QUAL_LOCAL_ADDR);
    } else if (variable == "%<INBOUND:LOCAL-PORT>") {
      ConditionInbound::append_value(resolved_variable, res, NET_QUAL_LOCAL_PORT);
    } else if (variable == "%<INBOUND:TLS>") {
      ConditionInbound::append_value(resolved_variable, res, NET_QUAL_TLS);
    } else if (variable == "%<INBOUND:H2>") {
      ConditionInbound::append_value(resolved_variable, res, NET_QUAL_H2);
    } else if (variable == "%<INBOUND:IPV4>") {
      ConditionInbound::append_value(resolved_variable, res, NET_QUAL_IPV4);
    } else if (variable == "%<INBOUND:IPV6>") {
      ConditionInbound::append_value(resolved_variable, res, NET_QUAL_IPV6);
    } else if (variable == "%<INBOUND:IP-FAMILY>") {
      ConditionInbound::append_value(resolved_variable, res, NET_QUAL_IP_FAMILY);
    } else if (variable == "%<INBOUND:STACK>") {
      ConditionInbound::append_value(resolved_variable, res, NET_QUAL_STACK);
    }

    // TODO(SaveTheRbtz): Can be optimized
    result.assign(first_part);
    result.append(resolved_variable);
    result.append(last_part);
  }

  return result;
}
