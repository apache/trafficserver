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

/**
 * @file test_aws_auth_v4.h
 * @brief TS API mock and and mock header iterator used for unit testing.
 * @see test_aws_auth_v4.cc
 */

#pragma once

#include <string> /* std::string */

/* Define a header iterator to be used in unit tests */
class HeaderIterator
{
public:
  HeaderIterator(const StringMap::iterator &it) { _it = it; }
  HeaderIterator(const HeaderIterator &i) { _it = i._it; }
  ~HeaderIterator() {}
  HeaderIterator &
  operator=(HeaderIterator &i)
  {
    _it = i._it;
    return *this;
  }
  HeaderIterator &
  operator++()
  {
    _it++;
    return *this;
  }
  HeaderIterator
  operator++(int)
  {
    HeaderIterator tmp(*this);
    operator++();
    return tmp;
  }
  bool
  operator!=(const HeaderIterator &it)
  {
    return _it != it._it;
  }
  const char *
  getName(int *len)
  {
    *len = _it->first.length();
    return _it->first.c_str();
  }
  const char *
  getValue(int *len)
  {
    *len = _it->second.length();
    return _it->second.c_str();
  }
  StringMap::iterator _it;
};

/* Define a mock API to be used in unit-tests */
class MockTsInterface : public TsInterface
{
public:
  const char *
  getMethod(int *length)
  {
    *length = _method.length();
    return _method.c_str();
  }
  const char *
  getHost(int *length)
  {
    *length = _host.length();
    return _host.c_str();
  }
  const char *
  getPath(int *length)
  {
    *length = _path.length();
    return _path.c_str();
  }
  const char *
  getQuery(int *length)
  {
    *length = _query.length();
    return _query.c_str();
  }
  HeaderIterator
  headerBegin()
  {
    return HeaderIterator(_headers.begin());
  }
  HeaderIterator
  headerEnd()
  {
    return HeaderIterator(_headers.end());
  }

  String _method;
  String _host;
  String _path;
  String _query;
  StringMap _headers;
};

/* Expose the following methods only to the unit tests */
String base16Encode(const char *in, size_t inLen);
String uriEncode(const String &in, bool isObjectName = false);
String uriDecode(const String &in);
String lowercase(const char *in, size_t inLen);
const char *trimWhiteSpaces(const char *in, size_t inLen, size_t &newLen);

String getCanonicalRequestSha256Hash(TsInterface &api, bool signPayload, const StringSet &includeHeaders,
                                     const StringSet &excludeHeaders, String &signedHeaders);
String getStringToSign(TsInterface &api, const char *dateTime, size_t dateTimeLen, const char *canonicalRequestSha256Hash,
                       size_t canonicalRequestSha256HashLen);
String getStringToSign(const char *host, size_t hostLen, const char *dateTime, size_t dateTimeLen, const char *region,
                       size_t regionLen, const char *service, size_t serviceLen, const char *canonicalRequestSha256Hash,
                       size_t canonicalRequestSha256HashLen);
String getRegion(const StringMap &regionMap, const char *host, size_t hostLen);
size_t hmacSha256(const char *secret, size_t secretLen, const char *msg, size_t msgLen, char *hmac, size_t hmacLen);

size_t getSignature(const char *awsSecret, size_t awsSecretLen, const char *awsRegion, size_t awsRegionLen, const char *awsService,
                    size_t awsServiceLen, const char *dateTime, size_t dateTimeLen, const char *stringToSign,
                    size_t stringToSignLen, char *base16Signature, size_t base16SignatureLen);
size_t getIso8601Time(time_t *now, char *dateTime, size_t dateTimeLen);

extern const StringMap defaultDefaultRegionMap;
extern const StringSet defaultExcludeHeaders;
extern const StringSet defaultIncludeHeaders;
