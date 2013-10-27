/**
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

/**
 * @file Headers.h
 */

#pragma once
#ifndef ATSCPPAPI_HEADERS_H_
#define ATSCPPAPI_HEADERS_H_

#include <map>
#include <list>
#include <atscppapi/CaseInsensitiveStringComparator.h>
#include <atscppapi/noncopyable.h>

namespace atscppapi {

struct HeadersState;
class Request;
class ClientRequest;
class Response;

/**
 * @brief Encapsulates the headers portion of a request or response.
 */
class Headers: noncopyable {
public:
  
  enum Type { TYPE_REQUEST, TYPE_RESPONSE };

  Headers(Type type = TYPE_REQUEST);

  Type getType() const;

  typedef std::map<std::string, std::list<std::string>, CaseInsensitiveStringComparator> NameValuesMap;

  typedef NameValuesMap::size_type size_type;
  typedef NameValuesMap::const_iterator const_iterator;
  typedef NameValuesMap::const_reverse_iterator const_reverse_iterator;

  /**
   * @return Iterator to first header.
   */
  const_iterator begin() const;

  /**
   * @return Iterator to end of headers.
   */
  const_iterator end() const;

  /**
   * @return Iterator to last header.
   */
  const_reverse_iterator rbegin() const;

  /**
   * @return Iterator to "reverse end"
   */
  const_reverse_iterator rend() const;

  /**
   * @param key Name of header
   * @return Iterator to header if exists, else end()
   */
  const_iterator find(const std::string &key) const;

  /**
   * @param key Name of header
   * @return 1 if header exists, 0 if not. 
   */
  size_type count(const std::string &key) const;

  /**
   * Erases header with given name.
   *
   * @param key Name of header
   * @return 1 if header was erased, 0 if not. 
   */
  size_type erase(const std::string &key);

  /**
   * Sets the given header and values. If a header of same name existed, that is
   * deleted. Else header is appended.
   * 
   * @param pair Contains the name and list of values.
   *
   * @return Iterator to header set. 
   */
  const_iterator set(const std::pair<std::string, std::list<std::string> > &pair);

  /**
   * Sets the given header and values. If a header of same name existed, that is
   * deleted. Else header is appended.
   *
   * @param key Name of header
   * @param val List of values
   * 
   * @return Iterator to header set. 
   */
  const_iterator set(const std::string &key, const std::list<std::string> &val);

  /**
   * Sets the given header and values. If a header of same name existed, that is
   * deleted. Else header is appended.
   *
   * @param key Name of header
   * @param val Value
   * 
   * @return Iterator to header set. 
   */
  const_iterator set(const std::string &key, const std::string &val);

  /**
   * Appends a new header. If a header of the same name exists, value(s) is tacked
   * on that the end of current value(s). 
   * 
   * @param pair Contains the name and list of values.
   *
   * @return Iterator to header appended. 
   */
  const_iterator append(const std::pair<std::string, std::list<std::string> > &pair);

  /**
   * Appends a new header. If a header of the same name exists, value(s) is tacked
   * on that the end of current value(s). 
   * 
   * @param key Name of header
   * @param val List of values
   * 
   * @return Iterator to header appended. 
   */
  const_iterator append(const std::string &key, const std::list<std::string> &val);

  /**
   * Appends a new header. If a header of the same name exists, value(s) is tacked
   * on that the end of current value(s). 
   * 
   * @param key Name of header
   * @param val Value
   * 
   * @return Iterator to header appended. 
   */
  const_iterator append(const std::string &key, const std::string &val);

  /**
   * Joins provided list of values with delimiter (defaulting to ',').
   *
   * @return Composite string
   */
  static std::string getJoinedValues(const std::list<std::string> &values, char delimiter = ',');

  /**
   * Joins values of provided header with delimiter (defaulting to ',').
   *
   * @return Composite string if header exists, else empty strings.
   */
  std::string getJoinedValues(const std::string &key, char value_delimiter = ',');

  /**
   * @return True if there are no headers.
   */
  bool empty() const;

  /**
   * @return Largest possible size of underlying map.
   */
  size_type max_size() const;

  /**
   * @return Number of headers currently stored.
   */
  size_type size() const;

  typedef std::map<std::string, std::list<std::string> > RequestCookieMap;

  /**
   * @return Cookies in the "Cookie" headers of a request object.
   */
  const RequestCookieMap &getRequestCookies() const;

  struct ResponseCookie {
    std::string name_;
    std::string value_;
    std::string comment_;
    std::string domain_;
    int max_age_;
    std::string path_;
    bool secure_;
    int version_;
    ResponseCookie() : max_age_(0), secure_(false), version_(0) { };
  };

  /**
   * @return Cookies in the "Set-Cookie" headers of a response object.
   */
  const std::list<ResponseCookie> &getResponseCookies() const;

  /** Adds a request cookie */
  bool addCookie(const std::string &name, const std::string &value);

  /** Adds a response cookie */
  bool addCookie(const ResponseCookie &response_cookie);
  
  /** Sets, i.e., clears current value and adds new value, of a request cookie */
  bool setCookie(const std::string &name, const std::string &value);

  /** Sets, i.e., clears current value and adds new value, of a response cookie */
  bool setCookie(const ResponseCookie &response_cookie);

  /** Deletes a cookie */
  bool deleteCookie(const std::string &name);

  ~Headers();
private:
  HeadersState *state_;
  bool checkAndInitHeaders() const;
  void init(void *hdr_buf, void *hdr_loc);
  void initDetached();
  void setType(Type type);
  void updateRequestCookieHeaderFromMap();
  const_iterator doBasicAppend(const std::pair<std::string, std::list<std::string> > &pair);
  size_type doBasicErase(const std::string &key);
  friend class Request;
  friend class ClientRequest;
  friend class Response;
};

}

#endif
