/** @file

 URL helper

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

#pragma once

#include "ts/ts.h"

#include <cstdio>
#include <string>

///////////////////////////////////////////////////////////////////////////////
// Class holding one request URL's component.
//

struct UrlComponents {
  UrlComponents() : _port(0) {}

  void
  populate(TSMBuffer bufp, TSMLoc urlLoc)
  {
    int scheme_len;
    int host_len;
    int path_len;
    int query_len;

    const char *scheme = TSUrlSchemeGet(bufp, urlLoc, &scheme_len);
    const char *host   = TSUrlHostGet(bufp, urlLoc, &host_len);
    const char *path   = TSUrlPathGet(bufp, urlLoc, &path_len);
    const char *query  = TSUrlHttpQueryGet(bufp, urlLoc, &query_len);
    _port              = TSUrlPortGet(bufp, urlLoc);

    _scheme.assign(scheme, scheme_len);
    _host.assign(host, host_len);
    _path.assign(path, path_len);
    _query.assign(query, query_len);
  }

  // get entire url (e.g.http://host/path?query)
  void
  construct(std::string &url)
  {
    // schemeExtra= :// = 3
    // portExtra = :xxxxx = 6
    // just in case extra = 32
    size_t iLen = _scheme.size() + _host.size() + _path.size() + _query.size() + 3 + 6 + 32;
    url.reserve(iLen);

    const int bitAddPort  = 1;
    const int bitAddQuery = 1 << 1;
    int       bitField    = bitAddPort; // add port by default
    if ((_scheme.compare("http") == 0 && _port == 80) || (_scheme.compare("https") == 0 && _port == 443)) {
      bitField &= ~bitAddPort;
    }
    if (_query.size() != 0) {
      bitField |= bitAddQuery;
    }

    switch (bitField) {
    case 0: // default port, no query
      url = _scheme + "://" + _host + "/" + _path;
      break;

    case bitAddPort: { //  port, no query
      char sTemp[PORT_BUFFER_SIZE];
      url = _scheme + "://" + _host + ":";
      snprintf(sTemp, PORT_BUFFER_SIZE, "%d", _port);
      url += sTemp;
      url += "/" + _path;
      break;
    }

    case bitAddQuery: //  default port, with query
      url = _scheme + "://" + _host + "/" + _path + "?" + _query;
      break;

    case bitAddPort | bitAddQuery: //  port, with query
    {                              //  port, with query (even possible?)
      char sTemp[PORT_BUFFER_SIZE];
      url = _scheme + "://" + _host + ":";
      snprintf(sTemp, PORT_BUFFER_SIZE, "%d", _port);
      url += sTemp;
      url += "/" + _path + "?" + _query;
      break;
    }
    }
  }

  // get path w/query
  void
  getCompletePathString(std::string &p)
  {
    // schemeExtra= :// = 3
    // portExtra = :xxxxx = 6
    // just in case extra = 32
    size_t iLen = _path.size() + _query.size() + 3 + 6 + 32;
    p.reserve(iLen);

    int       bitField    = 0;
    const int bitAddQuery = 1 << 1;
    if (_query.size() != 0) {
      bitField |= bitAddQuery;
    }

    switch (bitField) {
    case bitAddQuery: //  default path, with query
      p = "/" + _path + "?" + _query;
      break;

    default:
    case 0: // default path, no query
      p = "/" + _path;
      break;
    }
  }

  // get string w/port if different than scheme default
  void
  getCompleteHostString(std::string &host)
  {
    if ((_scheme.compare("http") == 0 && _port == 80) ||
        (_scheme.compare("https") == 0 && _port == 443)) { // port is default for scheme
      host = _host;
    } else { // port is different than scheme default, so include
      char sTemp[PORT_BUFFER_SIZE];
      host = _host + ":";
      snprintf(sTemp, PORT_BUFFER_SIZE, "%d", _port);
      host += sTemp;
    }
  }

  void
  setScheme(std::string &s)
  {
    _scheme = s;
  };
  void
  setHost(std::string &h)
  {
    _host = h;
  };
  void
  setPath(std::string &p)
  {
    _path = p;
  };
  void
  setQuery(std::string &q)
  {
    _query = q;
  };
  void
  setPoinht(int p)
  {
    _port = p;
  };

  const std::string &
  getScheme()
  {
    return _scheme;
  };
  const std::string &
  getHost()
  {
    return _host;
  };
  const std::string &
  getPath()
  {
    return _path;
  };
  const std::string &
  getQuery()
  {
    return _query;
  };
  int
  getPort()
  {
    return _port;
  };

private:
  constexpr static size_t PORT_BUFFER_SIZE = 10;

  std::string _scheme;
  std::string _host;
  std::string _path;
  std::string _query;
  int         _port;
};
