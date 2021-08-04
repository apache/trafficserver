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
 * @file Url.h
 */

#pragma once

#include <string>
#include <cstdint>
#include "tscpp/api/noncopyable.h"

namespace atscppapi
{
struct UrlState;

/**
 * @brief This class contains all properties of a Url.
 *
 * You can use a Url object to get and set any property of a url.
 *
 * @warning Url objects should never be constructed by the user.
 * If a user needs to create an unbound Url then they should create a Request
 * object using Request::Request(string) which will construct a Url object for them
 * and it can be retrieved via Request::getUrl().
 */
class Url : noncopyable
{
public:
  /**
   * @warning Url objects should never be constructed by the user.
   * If a user needs to create an unbound Url then they should create a Request
   * object using Request::Request(string) which will construct a Url object for them
   * and it can be retrieved via Request::getUrl().
   *
   * @private
   */
  Url();

  /**
   * @warning Url objects should never be constructed by the user.
   * If a user needs to create an unbound Url then they should create a Request
   * object using Request::Request(string) which will construct a Url object for them
   * and it can be retrieved via Request::getUrl().
   *
   * @private
   */
  Url(void *hdr_buf, void *url_loc);
  ~Url();

  /**
   * @return The full url as a string, such a url might be http://trafficserver.apache.org/search?q=blah
   */
  std::string getUrlString() const;

  /**
   * @return The path only portion of the url, such as /search
   */
  std::string getPath() const;

  /**
   * @return The query only portion of the url, which might be q=blah
   */
  std::string getQuery() const;

  /**
   * @return The scheme of the url, this will be either http or https.
   */
  std::string getScheme() const;

  /**
   * @return The host only of the url, this might be www.google.com
   */
  std::string getHost() const;

  /**
   * @return The port only portion of the url, this will likely be 80 or 443.
   */
  uint16_t getPort() const;

  /**
   * Set the path of the url.
   * @param path the path portion of the url to set, this might be something like /foo/bar
   */
  void setPath(const std::string &);

  /**
   * Set the query param of the url.
   * @param query the query portion of the url, this might be something like foo=bar&blah=baz.
   */
  void setQuery(const std::string &);

  /**
   * Set the scheme of the url
   * @param scheme this might be either http or https.
   */
  void setScheme(const std::string &);

  /**
   * Set the host of the url
   * @param host this might be something such as www.linkedin.com or www.apache.org
   */
  void setHost(const std::string &);

  /**
   * Set the port portion of the url.
   * @param port this is a uint16_t which represents the port (in host order, there is no need to convert to network order). You
   * might use a value such as 80 or 8080.
   */
  void setPort(const uint16_t);

private:
  bool isInitialized() const;
  void init(void *hdr_buf, void *url_loc);
  UrlState *state_;
  friend class Request;
  friend class ClientRequest;
  friend class RemapPlugin;
};
} // namespace atscppapi
