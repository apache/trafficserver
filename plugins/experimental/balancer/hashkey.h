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

//////////////////////////////////////////////////////////////////////////////////////////////
// 
// Implement the classes for the various types of hash keys we support.
//
#ifndef __HASHKEY_H__
#define __HASHKEY_H__ 1


#include <string>

#include <ts/remap.h>
#include <ts/ts.h>

#include "resources.h"


///////////////////////////////////////////////////////////////////////////////
// Base class for all HashKeys
//
class HashKey
{
public:
  HashKey() :
    next(NULL)
  { }

  virtual ~HashKey()
  { }


  // Return the number of bytes of data to use from the data pointer. If we
  // return (-1), no data is available for this particular request.
  virtual int key(const void** data, Resources& resr) const = 0;

  virtual void free_key(const void* data, int len, Resources& resr) const {
    // No-op by default
  }

  void append(HashKey* hash) {
    TSReleaseAssert(hash->next == NULL);

    if (NULL == next) {
      next = hash;
    } else {
      HashKey* tmp = next;

      while (tmp->next)
        tmp = next->next;

      tmp->next = hash;
    }
  }

  HashKey* next;
};


///////////////////////////////////////////////////////////////////////////////
// Class for a URL based Hash Key. Set the data pointer to the full URL.
//
class URLHashKey : public HashKey
{
 public:
  int
  key(const void** data, Resources& resr) const {
    *data = resr.getRRI()->orig_url;
    return resr.getRRI()->orig_url_size;
  }
};


///////////////////////////////////////////////////////////////////////////////
// Class for a Path based Hash Key. Set the data pointer to the path only.
//
class PathHashKey : public HashKey
{
 public:
  int
  key(const void** data, Resources& resr) const {
    *data = resr.getRRI()->request_path;
    return resr.getRRI()->request_path_size;
  }
};


///////////////////////////////////////////////////////////////////////////////
// Class for a Cookie based Hash Key. Set the data pointer to the Cookie
// selected, or NULL if not available.
//
class CookieHashKey : public HashKey
{
 public:
  CookieHashKey(const std::string cookie) {
    std::string::size_type dot = cookie.find_first_of(".");

    if (dot != std::string::npos) {
      std::string tmp;

      tmp = cookie.substr(0, dot);
      _main = TSstrdup(tmp.c_str());
      _main_len = dot;
      tmp = cookie.substr(dot + 1);
      if (tmp.size() > 0) {
        _sub = TSstrdup(tmp.c_str());
        _sub_len = cookie.size() - dot - 1;
      } else {
        _sub = NULL;
        _sub_len = 1;
      }
    } else {
      _main = TSstrdup(cookie.c_str());
      _main_len = cookie.size();
      _sub = NULL;
      _sub_len = 0;
    }
  }

  ~CookieHashKey() {
    if (_main)
      TSfree(const_cast<char*>(_main));
    if (_sub)
      TSfree(const_cast<char*>(_sub));
  }

  int
  key(const void** data, Resources& resr) const {
    if (_main) {
      if (resr.getJar()) {
        const char* cookie;

        if (_sub) {
          cookie = // TODO - get sub cookie
        } else {
          cookie = // TODO - get full cookie
        }
        if (cookie) {
          *data = cookie;
          return strlen(cookie);
        }
      }
    } else {
      if (resr.getRRI()->request_cookie_size > 0) {
        *data = resr.getRRI()->request_cookie;
        return resr.getRRI()->request_cookie_size;
      }
    }

    // Nothing found
    *data = NULL;
    return -1;
  }

 private:
  const char* _main;
  const char* _sub;
  int _main_len, _sub_len;
};


///////////////////////////////////////////////////////////////////////////////
// Class for a IP based Hash Key. Set the data pointer to the IP (in network
// byte order).
//
class IPHashKey : public HashKey
{
 public:
  int
  key(const void** data, Resources& resr) const {
    *data = &(resr.getRRI()->client_ip);
    return 4; // ToDo: This only works with IPV4, obviously
  }
};


///////////////////////////////////////////////////////////////////////////////
// Class for a Header based Hash Key. Set the data pointer to the Header, or
// NULL if not available.
//
class HeaderHashKey : public HashKey
{
 public:
  HeaderHashKey(const std::string header) {
    _header = TSstrdup(header.c_str());
    _header_len = header.size();
  }

  ~HeaderHashKey() {
    if (_header)
      TSfree(const_cast<char*>(_header));
  }

  int
  key(const void** data, Resources& resr) const {
    TSMBuffer bufp = resr.getBufp();
    TSMLoc hdrLoc = resr.getHdrLoc();
    TSMLoc fieldLoc;
    const char* val;
    int len = -1;

    // Note that hdrLoc is freed as part of the Resources dtor, and we free the "string" value
    // in the free_key() implementation (after we're done with it).
    if (bufp && hdrLoc && (fieldLoc = TSMimeHdrFieldFind(bufp, hdrLoc, _header, _header_len))) {
      if (TS_ERROR != TSMimeHdrFieldValueStringGet(bufp, hdrLoc, fieldLoc, 0, &val, &len)) {
        *data = val;
      } else {
        *data = NULL;
      }
      TSHandleMLocRelease(bufp, hdrLoc, fieldLoc);
    } else {
      *data = NULL;
    }

    return len;
  }

  void free_key(const void* data, int len, Resources& resr) const {
    TSMBuffer bufp = resr.getBufp();
    TSMLoc hdrLoc = resr.getHdrLoc();

    if (bufp && hdrLoc)
      TSHandleStringRelease(bufp, hdrLoc, (const char*)data);
  }

 private:
  const char* _header;
  int _header_len;
};


#endif // __HASHKEY_H

