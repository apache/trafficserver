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

#include "Variables.h"
#include "Attribute.h"
#include "Utils.h"

#include <errno.h>

using std::list;
using std::pair;
using std::string;
using namespace EsiLib;

const string Variables::EMPTY_STRING("");
const string Variables::TRUE_STRING("true");
const string Variables::VENDOR_STRING("vendor");
const string Variables::VERSION_STRING("version");
const string Variables::PLATFORM_STRING("platform");
const string Variables::SIMPLE_HEADERS[] = { string("HOST"),
                                             string("REFERER"),
                                             string("") };

const string Variables::SPECIAL_HEADERS[] = { string("ACCEPT-LANGUAGE"),
                                              string("COOKIE"),
                                              string("USER-AGENT"),
                                              string("QUERY_STRING"),
                                              string("") };

const string Variables::NORM_SIMPLE_HEADERS[] = { string("HTTP_HOST"),
                                                  string("HTTP_REFERER"),
                                                  string("") };

const string Variables::NORM_SPECIAL_HEADERS[] = { string("HTTP_ACCEPT_LANGUAGE"),
                                                   string("HTTP_COOKIE"),
                                                   string("HTTP_USER_AGENT"),
                                                   string("QUERY_STRING"),
                                                   string("") };

inline string &
Variables::_toUpperCase(string &str) const {
  for (size_t i = 0; i < str.size(); ++i) {
    if ((str[i] >= 'a') && (str[i] <= 'z')) {
      str[i] = 'A' + (str[i] - 'a');
    }
  }
  return str;
}

inline int 
Variables::_searchHeaders(const string headers[], const char *name, int name_len) const {
  int curr_header_size;
  for (int i = 0; (curr_header_size = static_cast<int>(headers[i].size())); ++i) {
    if ((name_len == curr_header_size) && 
        (strncasecmp(headers[i].data(), name, curr_header_size) == 0)) {
      return i;
    }
  }
  return -1;
}

inline void
Variables::_insert(StringHash &hash, const std::string &key, const std::string &value) {
  std::pair<StringHash::iterator, bool> result = hash.insert(StringHash::value_type(key, value));
  if (!result.second) {
    result.first->second = value;
  }
}

void
Variables::populate(const HttpHeader &header) {
  if (header.name && header.name_len && header.value && header.value_len) {

    // doing this because we can't change our const input arg
    int name_len = (header.name_len == -1) ? strlen(header.name) : header.name_len;
    int value_len = (header.value_len == -1) ? strlen(header.value) : header.value_len;

    // we need to save the cookie string to build the jar from
    if ((name_len == 6) && (strncasecmp(header.name, "Cookie", 6) == 0)) {
      _releaseCookieJar();
      if (_cookie_str.size()) {
        _cookie_str.append(", ");
      }
      _cookie_str.append(header.value, value_len);
    }

    if (_headers_parsed) {
      _parseHeader(header.name, name_len, header.value, value_len);
    } else {
      int match_index = _searchHeaders(SIMPLE_HEADERS, header.name, name_len);
      if (match_index != -1) {
        _cached_simple_headers[match_index].push_back(string(header.value, value_len));
      } else {
        match_index = _searchHeaders(SPECIAL_HEADERS, header.name, name_len);
        if (match_index != -1) {
          _cached_special_headers[match_index].push_back(string(header.value, value_len));
        } else {
          _debugLog(_debug_tag.c_str(), "[%s] Not retaining header [%.*s]", __FUNCTION__, name_len,
                    header.name);
        }
      }
    }
  }
}

inline void
Variables::_parseSimpleHeader(SimpleHeader hdr, const string &value) {
  _debugLog(_debug_tag.c_str(), "[%s] Inserting value for simple header [%s]",
            __FUNCTION__, SIMPLE_HEADERS[hdr].c_str());
  _simple_data[NORM_SIMPLE_HEADERS[hdr]] = value;
}

inline void
Variables::_parseSimpleHeader(SimpleHeader hdr, const char *value, int value_len) {
  _parseSimpleHeader(hdr, string(value, value_len));
}

void
Variables::_parseSpecialHeader(SpecialHeader hdr, const char *value, int value_len) {
  switch (hdr) {
  case HTTP_ACCEPT_LANGUAGE:
    _parseAcceptLangString(value, value_len);
    break;
  case HTTP_COOKIE:
    _parseCookieString(value, value_len);
    break;
  case HTTP_USER_AGENT:
    _parseUserAgentString(value, value_len);
    break;
  default:
    _debugLog(_debug_tag.c_str(), "[%s] Skipping unrecognized header", __FUNCTION__);
    break;
  }
}

void
Variables::_parseHeader(const char *name, int name_len, const char *value, int value_len) {
  int match_index = _searchHeaders(SIMPLE_HEADERS, name, name_len);
  if (match_index != -1) {
    _parseSimpleHeader(static_cast<SimpleHeader>(match_index), value, value_len);
  } else {
    match_index = _searchHeaders(SPECIAL_HEADERS, name, name_len);
    if (match_index != -1) {
      _parseSpecialHeader(static_cast<SpecialHeader>(match_index), value, value_len);
    } else {
      _debugLog(_debug_tag.c_str(), "[%s] Unrecognized header [%.*s]", __FUNCTION__, value_len, value);
    }
  }
}

void
Variables::_parseQueryString(const char *query_string, int query_string_len) {
  _insert(_simple_data, string("QUERY_STRING"), string(query_string, query_string_len));
  AttributeList attr_list;
  Utils::parseAttributes(query_string, query_string_len, attr_list, "&");
  for (AttributeList::iterator iter = attr_list.begin(); iter != attr_list.end(); ++iter) {
    _debugLog(_debug_tag.c_str(), "[%s] Inserting query string variable [%.*s] with value [%.*s]",
              __FUNCTION__, iter->name_len, iter->name, iter->value_len, iter->value);
    _insert(_dict_data[QUERY_STRING], string(iter->name, iter->name_len),
            string(iter->value, iter->value_len));
  }
}

void
Variables::_parseCachedHeaders() {
  _debugLog(_debug_tag.c_str(), "[%s] Parsing headers", __FUNCTION__);
  for (int i = 0; i < N_SIMPLE_HEADERS; ++i) {
    for (HeaderValueList::iterator value_iter = _cached_simple_headers[i].begin();
         value_iter != _cached_simple_headers[i].end(); ++value_iter) {
      _parseSimpleHeader(static_cast<SimpleHeader>(i), *value_iter);
    }
  }
  for (int i = 0; i < N_SPECIAL_HEADERS; ++i) {
    for (HeaderValueList::iterator value_iter = _cached_special_headers[i].begin();
         value_iter != _cached_special_headers[i].end(); ++value_iter) {
      _parseSpecialHeader(static_cast<SpecialHeader>(i), value_iter->data(), value_iter->size());
    }
  }
}

const std::string &
Variables::getValue(const string &name) const {
  if (!_headers_parsed || !_query_string_parsed) {
    // we need to do this because we want to
    // 1) present const getValue() to clients
    // 2) parse lazily (only on demand)
    Variables &non_const_self = const_cast<Variables &>(*this);
    if (!_headers_parsed) {
      non_const_self._parseCachedHeaders();
      non_const_self._headers_parsed = true;
    }
    if (!_query_string_parsed) {
      int query_string_size = static_cast<int>(_query_string.size());
      if (query_string_size) {
        non_const_self._parseQueryString(_query_string.data(), query_string_size);
        non_const_self._query_string_parsed = true;
      }
    }
  }
  string search_key(name);
  _toUpperCase(search_key);
  StringHash::const_iterator iter = _simple_data.find(search_key);
  if (iter != _simple_data.end()) {
    _debugLog(_debug_tag.c_str(), "[%s] Found value [%.*s] for variable [%.*s] in simple data", 
              __FUNCTION__, iter->second.size(), iter->second.data(), name.size(), name.data());
    return iter->second;
  }
  const char *header;
  int header_len;
  const char *attr;
  int attr_len;
  if (!_parseDictVariable(name, header, header_len, attr, attr_len)) {
    _debugLog(_debug_tag.c_str(), "[%s] Unmatched simple variable [%.*s] not in dict variable form",
              __FUNCTION__, name.size(), name.data());
    return EMPTY_STRING;
  }
  int dict_index = _searchHeaders(NORM_SPECIAL_HEADERS, header, header_len); // ignore the HTTP_ prefix
  if (dict_index == -1) {
    _debugLog(_debug_tag.c_str(), "[%s] Dict variable [%.*s] refers to unknown dictionary",
              __FUNCTION__, name.size(), name.data());
    return EMPTY_STRING;
  }

  // change variable name to use only the attribute field
  search_key.assign(attr, attr_len);

  iter = _dict_data[dict_index].find(search_key);

  if (dict_index == HTTP_ACCEPT_LANGUAGE) {
    _debugLog(_debug_tag.c_str(), "[%s] Returning boolean literal for lang variable [%.*s]",
              __FUNCTION__, search_key.size(), search_key.data());
    return (iter == _dict_data[dict_index].end()) ? EMPTY_STRING : TRUE_STRING;
  }
  
  if (iter != _dict_data[dict_index].end()) {
    _debugLog(_debug_tag.c_str(), "[%s] Found variable [%.*s] in %s dictionary with value [%.*s]",
              __FUNCTION__, search_key.size(), search_key.data(), NORM_SPECIAL_HEADERS[dict_index].c_str(), 
              iter->second.size(), iter->second.data());
    return iter->second;
  }

  size_t cookie_part_divider = (dict_index == HTTP_COOKIE) ? search_key.find(';') : search_key.size();
  if (cookie_part_divider && (cookie_part_divider < (search_key.size() - 1))) {
    _debugLog(_debug_tag.c_str(), "[%s] Cookie variable [%s] refers to sub cookie", 
              __FUNCTION__, search_key.c_str());
    return _getSubCookieValue(search_key, cookie_part_divider);
  }
  
  _debugLog(_debug_tag.c_str(), "[%s] Found no value for dict variable [%s]", __FUNCTION__, name.c_str());
  return EMPTY_STRING;
}

const string &
Variables::_getSubCookieValue(const string &cookie_str, size_t cookie_part_divider) const {
  if (!_cookie_jar_created) {
    if (_cookie_str.size()) {
      Variables &non_const_self = const_cast<Variables &>(*this); // same reasoning as in getValue()
      // TODO - code was here
      non_const_self._cookie_jar_created = true;
    } else {
      _debugLog(_debug_tag.c_str(), "[%s] Cookie string empty; nothing to construct jar from", __FUNCTION__);
    }
  }
  if (_cookie_jar_created) {
    // we need to do this as we are going to manipulate the 'divider'
    // character, and we don't need to create a copy of the string for
    // that; hence this shortcut
    string &non_const_cookie_str = const_cast<string &>(cookie_str);
    
    non_const_cookie_str[cookie_part_divider] = '\0'; // make sure cookie name is NULL terminated
    const char *cookie_name = non_const_cookie_str.data(); /* above NULL will take effect */
    const char *part_name = 
      non_const_cookie_str.c_str() /* NULL terminate the part */ + cookie_part_divider + 1;
    bool user_name = (part_name[0] == 'u') && (part_name[1] == '\0');
    if (user_name) {
      part_name = "l";
    }
    // TODO - code was here
    const char *sub_cookie_value = NULL;
    non_const_cookie_str[cookie_part_divider] = ';'; // restore before returning
    if (!sub_cookie_value) {
      _debugLog(_debug_tag.c_str(), "[%s] Could not find value for part [%s] of cookie [%.*s]", __FUNCTION__,
                part_name, cookie_part_divider, cookie_name);
      return EMPTY_STRING;
    } else {
      // we need to do this as have to return a string reference
      string &retval = const_cast<Variables &>(*this)._cached_sub_cookie_value;

      if (user_name) {
        char unscrambled_login[256];
        // TODO - code was here
        _debugLog(_debug_tag.c_str(), "[%s] Unscrambled login name to [%s]", __FUNCTION__, unscrambled_login);
        retval.assign(unscrambled_login);
      } else {
        _debugLog(_debug_tag.c_str(), "[%s] Got value [%s] for cookie name [%.*s] and part [%s]",
                  __FUNCTION__, sub_cookie_value, cookie_part_divider, cookie_name, part_name);
        retval.assign(sub_cookie_value);
      }
      return retval;
    }
  } else {
    _errorLog("[%s] Cookie jar not available; Returning empty string", __FUNCTION__);
    return EMPTY_STRING;
  }
}

void
Variables::clear() {
  _simple_data.clear();
  for (int i = 0; i < N_SPECIAL_HEADERS; ++i) {
    _dict_data[i].clear();
    _cached_special_headers[i].clear();
  }
  for (int i = 0; i < N_SIMPLE_HEADERS; ++i) {
    _cached_simple_headers[i].clear();
  }
  _query_string.clear();
  _headers_parsed = _query_string_parsed = false;
  _cookie_str.clear();
  _releaseCookieJar();
}

void
Variables::_parseCookieString(const char *str, int str_len) {
  AttributeList cookies;
  Utils::parseAttributes(str, str_len, cookies, ";,");
  for (AttributeList::iterator iter = cookies.begin(); iter != cookies.end(); ++iter) {
    _insert(_dict_data[HTTP_COOKIE], string(iter->name, iter->name_len), string(iter->value, iter->value_len));
    _debugLog(_debug_tag.c_str(), "[%s] Inserted cookie with name [%.*s] and value [%.*s]", __FUNCTION__,
              iter->name_len, iter->name, iter->value_len, iter->value);
  }
}

void
Variables::_parseUserAgentString(const char *str, int str_len) {
  string user_agent_str(str, str_len); // need NULL-terminated version
  // TODO - code was here
  char version_buf[64];
  // TODO - code was here
  _insert(_dict_data[HTTP_USER_AGENT], VERSION_STRING, version_buf);
}

void
Variables::_parseAcceptLangString(const char *str, int str_len) {
  int i;
  for(i = 0; (i < str_len) && ((isspace(str[i]) || str[i] == ',')); ++i);
  const char *lang = str + i;
  int lang_len;
  for (; i <= str_len; ++i) {
    if ((i == str_len) || (str[i] == ',')) {
      lang_len = str + i - lang;
      for (; lang_len && isspace(lang[lang_len - 1]); --lang_len);
      if (lang_len) {
        _insert(_dict_data[HTTP_ACCEPT_LANGUAGE], string(lang, lang_len), EMPTY_STRING);
        _debugLog(_debug_tag.c_str(), "[%s] Added language [%.*s]", __FUNCTION__, lang_len, lang);
      }
      for(; (i < str_len) && ((isspace(str[i]) || str[i] == ',')); ++i);
      lang = str + i;
    }
  }
}

bool
Variables::_parseDictVariable(const std::string &variable, const char *&header, int &header_len,
                              const char *&attr, int &attr_len) const {
  const char *var_ptr = variable.data();
  int var_size = variable.size();
  if ((var_size <= 4) || (variable[var_size - 1] != '}')) {
    return false;
  }
  int paranth_index = -1;
  for (int i = 0; i < (var_size - 1); ++i) {
    if (variable[i] == '{') {
      if (paranth_index != -1) {
        _debugLog(_debug_tag.c_str(), "[%s] Cannot have multiple paranthesis in dict variable [%.*s]",
                  __FUNCTION__, var_size, var_ptr);
        return false;
      }
      paranth_index = i;
    }
    if (variable[i] == '}') {
      _debugLog(_debug_tag.c_str(), "[%s] Cannot have multiple paranthesis in dict variable [%.*s]",
                __FUNCTION__, var_size, var_ptr);
      return false;
    }
  }
  if (paranth_index == -1) {
    _debugLog(_debug_tag.c_str(), "[%s] Could not find opening paranthesis in variable [%.*s]",
              __FUNCTION__, var_size, var_ptr);
    return false;
  }
  if (paranth_index == 0) {
    _debugLog(_debug_tag.c_str(), "[%s] Dict variable has no dict name [%.*s]",
              __FUNCTION__, var_size, var_ptr);
    return false;
  }
  if (paranth_index == (var_size - 2)) {
    _debugLog(_debug_tag.c_str(), "[%s] Dict variable has no attribute name [%.*s]",
              __FUNCTION__, var_size, var_ptr);
    return false;
  }
  header = var_ptr;
  header_len = paranth_index;
  attr = var_ptr + paranth_index + 1;
  attr_len = var_size - header_len - 2;
  return true;
}
