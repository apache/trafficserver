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
// Public interface for creating all operators. Don't user the operator.h interface
// directly!
//
#ifndef __OPERATOR_H__
#define __OPERATOR_H__ 1

#include <string>
#include <ts/ts.h>

#include "resources.h"
#include "statement.h"
#include "parser.h"

#include <iostream>
#include <arpa/inet.h>
#include <sstream>

// Operator modifiers
enum OperModifiers {
  OPER_NONE = 0,
  OPER_LAST = 1,
  OPER_NEXT = 2,
  OPER_QSA = 4
};


///////////////////////////////////////////////////////////////////////////////
// Base class for all Operators (this is also the interface)
//
class Operator : public Statement
{
public:
  Operator()
    : _mods(OPER_NONE)
  {
    TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for Operator");
  }

  void do_exec(const Resources& res) const {
    exec(res);
    if (NULL != _next)
      static_cast<Operator*>(_next)->do_exec(res);
  }

  const OperModifiers get_oper_modifiers() const;

  virtual void initialize(Parser& p);

protected:
  virtual void exec(const Resources& res) const = 0;

private:
  DISALLOW_COPY_AND_ASSIGN(Operator);

  OperModifiers _mods;
};


///////////////////////////////////////////////////////////////////////////////
// Base class for all Header based Operators, this is obviously also an
// Operator interface.
//
class OperatorHeaders : public Operator
{
public:
  OperatorHeaders()
    : _header("")
  {
    TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for OperatorHeaders");
  }

  void initialize(Parser& p);

protected:
  std::string _header;

private:
  DISALLOW_COPY_AND_ASSIGN(OperatorHeaders);
};


class VariableExpander {
private:
  std::string _source;
public:
  VariableExpander(const std::string &source) :
      _source(source) {
  }

  std::string expand(const Resources& res) {
    std::string result;
    result.reserve(512); // TODO: Can be optimized
    result.assign(_source);

    while (true) {
      std::string::size_type start = result.find("%<");
      if (start == std::string::npos)
        break;

      std::string::size_type end = result.find(">", start);
      if (end == std::string::npos)
        break;

      std::string first_part = result.substr(0, start);
      std::string last_part = result.substr(end + 1);

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
          resolved_variable = TSUrlSchemeGet(bufp, url_loc, &len);
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
      }

      // TODO(SaveTheRbtz): Can be optimized
      result.assign(first_part);
      result.append(resolved_variable);
      result.append(last_part);
    }

    return result;
  }

private:
  std::string getIP(sockaddr const * s_sockaddr) {
    const struct sockaddr_in *s_sockaddr_in;
    const struct sockaddr_in6 *s_sockaddr_in6;

    if (s_sockaddr == NULL)
      return "";

    char res[INET6_ADDRSTRLEN] = { '\0' };

    switch (s_sockaddr->sa_family) {
      case AF_INET:
        s_sockaddr_in = reinterpret_cast<const struct sockaddr_in *>(s_sockaddr);
        inet_ntop(s_sockaddr_in->sin_family, &s_sockaddr_in->sin_addr, res, INET_ADDRSTRLEN);
        break;
      case AF_INET6:
        s_sockaddr_in6 = reinterpret_cast<const struct sockaddr_in6 *>(s_sockaddr);
        inet_ntop(s_sockaddr_in6->sin6_family, &s_sockaddr_in6->sin6_addr, res, INET6_ADDRSTRLEN);
        break;
    }

    return std::string(res);
  }

  std::string getURL(const Resources& res) {
    TSMBuffer bufp;
    TSMLoc url_loc;

    if (TSHttpTxnPristineUrlGet(res.txnp, &bufp, &url_loc) != TS_SUCCESS)
      return "";

    int url_len;
    char *url = TSUrlStringGet(bufp, url_loc, &url_len);
    std::string ret;
    if (url && url_len)
      ret.assign(url, url_len);
    TSfree(url);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, url_loc);

    return ret;
  }
};

#endif // __OPERATOR_H
