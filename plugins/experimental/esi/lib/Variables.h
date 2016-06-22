/** @file

  A brief file description

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

#ifndef _ESI_VARIABLES_H

#define _ESI_VARIABLES_H

#include <list>

#include <cstring>
#include "ComponentBase.h"
#include "StringHash.h"
#include "HttpHeader.h"

namespace EsiLib
{
class Variables : private ComponentBase
{
public:
  Variables(const char *debug_tag, ComponentBase::Debug debug_func, ComponentBase::Error error_func)
    : ComponentBase(debug_tag, debug_func, error_func),
      _headers_parsed(false),
      _query_string(""),
      _query_string_parsed(false),
      _cookie_jar_created(false){};

  /** currently 'host', 'referer', 'accept-language', 'cookie' and 'user-agent' headers are parsed */
  void populate(const HttpHeader &header);

  void
  populate(const HttpHeaderList &headers)
  {
    for (HttpHeaderList::const_iterator iter = headers.begin(); iter != headers.end(); ++iter) {
      populate(*iter);
    }
  };

  void
  populate(const char *query_string, int query_string_len = -1)
  {
    if (query_string && (query_string_len != 0)) {
      if (query_string_len == -1) {
        query_string_len = strlen(query_string);
      }
      if (_query_string_parsed) {
        _parseQueryString(query_string, query_string_len);
      } else {
        _query_string.assign(query_string, query_string_len);
      }
    }
  }

  /** returns value of specified variable; empty string returned for unknown variable; key
   * has to be prefixed with 'http_' string for all variable names except 'query_string' */
  const std::string &getValue(const std::string &name) const;

  /** convenient alternative for method above */
  const std::string &
  getValue(const char *name, int name_len = -1) const
  {
    if (!name) {
      return EMPTY_STRING;
    }
    std::string var_name;
    if (name_len == -1) {
      var_name.assign(name);
    } else {
      var_name.assign(name, name_len);
    }
    return getValue(var_name);
  }

  void clear();

  virtual ~Variables() { _releaseCookieJar(); };
private:
  Variables(const Variables &);            // non-copyable
  Variables &operator=(const Variables &); // non-copyable

  static const std::string EMPTY_STRING;
  static const std::string TRUE_STRING;
  static const std::string VENDOR_STRING;
  static const std::string VERSION_STRING;
  static const std::string PLATFORM_STRING;

  enum SimpleHeader {
    HTTP_HOST    = 0,
    HTTP_REFERER = 1,
  };
  static const std::string SIMPLE_HEADERS[]; // indices should map to enum values above

  enum SpecialHeader {
    HTTP_ACCEPT_LANGUAGE = 0,
    HTTP_COOKIE          = 1,
    HTTP_USER_AGENT      = 2,
    QUERY_STRING         = 3,
    HTTP_HEADER          = 4,
  };
  static const std::string SPECIAL_HEADERS[]; // indices should map to enum values above

  // normalized versions of the headers above; indices should correspond correctly
  static const std::string NORM_SIMPLE_HEADERS[];
  static const std::string NORM_SPECIAL_HEADERS[]; // indices should again map to enum values

  static const int N_SIMPLE_HEADERS  = HTTP_REFERER + 1;
  static const int N_SPECIAL_HEADERS = HTTP_HEADER + 1;

  StringHash _simple_data;
  StringHash _dict_data[N_SPECIAL_HEADERS];

  inline std::string &_toUpperCase(std::string &str) const;
  inline int _searchHeaders(const std::string headers[], const char *name, int name_len) const;
  bool _parseDictVariable(const std::string &variable, const char *&header, int &header_len, const char *&attr,
                          int &attr_len) const;
  void _parseCookieString(const char *str, int str_len);
  void _parseUserAgentString(const char *str, int str_len);
  void _parseAcceptLangString(const char *str, int str_len);
  inline void _parseSimpleHeader(SimpleHeader hdr, const std::string &value);
  inline void _parseSimpleHeader(SimpleHeader hdr, const char *value, int value_len);
  void _parseSpecialHeader(SpecialHeader hdr, const char *value, int value_len);
  void _parseCachedHeaders();

  inline void _insert(StringHash &hash, const std::string &key, const std::string &value);

  typedef std::list<std::string> HeaderValueList;
  HeaderValueList _cached_simple_headers[N_SIMPLE_HEADERS];
  HeaderValueList _cached_special_headers[N_SPECIAL_HEADERS];

  std::string _cookie_str;
  bool _headers_parsed;
  std::string _query_string;
  bool _query_string_parsed;

  void _parseHeader(const char *name, int name_len, const char *value, int value_len);
  void _parseQueryString(const char *query_string, int query_string_len);

  StringKeyHash<StringHash> _sub_cookies;
  bool _cookie_jar_created;

  void _parseSubCookies();

  inline void
  _releaseCookieJar()
  {
    if (_cookie_jar_created) {
      _sub_cookies.clear();
      _cookie_jar_created = false;
    }
  }

  mutable std::string _cached_sub_cookie_value;
  const std::string &_getSubCookieValue(const std::string &cookie_str, size_t cookie_part_divider) const;
};
};

#endif // _ESI_VARIABLES_H
