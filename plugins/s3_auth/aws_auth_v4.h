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
 * @file aws_auth_v4.h
 * @brief AWS Auth v4 signing utility.
 * @see aws_auth_v4.cc
 */

#ifndef PLUGINS_S3_AUTH_AWS_AUTH_V4_CC_
#define PLUGINS_S3_AUTH_AWS_AUTH_V4_CC_

#include <algorithm> /* transform() */
#include <cstddef>   /* soze_t */
#include <string>    /* std::string */
#include <sstream>   /* std::stringstream */
#include <map>       /* std::map */
#include <set>       /* std::set */

#include <ts/ts.h>

typedef std::string String;
typedef std::set<std::string> StringSet;
typedef std::map<std::string, std::string> StringMap;

class HeaderIterator;

class TsInterface
{
public:
  virtual ~TsInterface(){};
  virtual const char *getMethod(int *length) = 0;
  virtual const char *getHost(int *length)   = 0;
  virtual const char *getPath(int *length)   = 0;
  virtual const char *getQuery(int *length)  = 0;
  virtual HeaderIterator headerBegin()       = 0;
  virtual HeaderIterator headerEnd()         = 0;
};

/* Define a header iterator to be used in the plugin using ATS API */
class HeaderIterator
{
public:
  HeaderIterator() : _bufp(nullptr), _hdrs(TS_NULL_MLOC), _field(TS_NULL_MLOC) {}
  HeaderIterator(TSMBuffer bufp, TSMLoc hdrs, TSMLoc field) : _bufp(bufp), _hdrs(hdrs), _field(field) {}
  HeaderIterator(const HeaderIterator &it)
  {
    _bufp  = it._bufp;
    _hdrs  = it._hdrs;
    _field = it._field;
  }
  ~HeaderIterator() {}
  HeaderIterator &
  operator=(HeaderIterator &it)
  {
    _bufp  = it._bufp;
    _hdrs  = it._hdrs;
    _field = it._field;
    return *this;
  }
  HeaderIterator &operator++()
  {
    /* @todo this is said to be slow in the API call comments, do something better here */
    TSMLoc next = TSMimeHdrFieldNext(_bufp, _hdrs, _field);
    TSHandleMLocRelease(_bufp, _hdrs, _field);
    _field = next;
    return *this;
  }
  HeaderIterator operator++(int)
  {
    HeaderIterator tmp(*this);
    operator++();
    return tmp;
  }
  bool
  operator!=(const HeaderIterator &it)
  {
    return _bufp != it._bufp || _hdrs != it._hdrs || _field != it._field;
  }
  bool
  operator==(const HeaderIterator &it)
  {
    return _bufp == it._bufp && _hdrs == it._hdrs && _field == it._field;
  }
  const char *
  getName(int *len)
  {
    return TSMimeHdrFieldNameGet(_bufp, _hdrs, _field, len);
  }
  const char *
  getValue(int *len)
  {
    return TSMimeHdrFieldValueStringGet(_bufp, _hdrs, _field, -1, len);
  }
  TSMBuffer _bufp;
  TSMLoc _hdrs;
  TSMLoc _field;
};

/* Define a API to be used in the plugin using ATS API */
class TsApi : public TsInterface
{
public:
  TsApi(TSMBuffer bufp, TSMLoc hdrs, TSMLoc url) : _bufp(bufp), _hdrs(hdrs), _url(url) {}
  ~TsApi() {}
  const char *
  getMethod(int *len)
  {
    return TSHttpHdrMethodGet(_bufp, _hdrs, len);
  }
  const char *
  getHost(int *len)
  {
    return TSHttpHdrHostGet(_bufp, _hdrs, len);
  }
  const char *
  getPath(int *len)
  {
    return TSUrlPathGet(_bufp, _url, len);
  }
  const char *
  getQuery(int *len)
  {
    return TSUrlHttpQueryGet(_bufp, _url, len);
  }
  HeaderIterator
  headerBegin()
  {
    return HeaderIterator(_bufp, _hdrs, TSMimeHdrFieldGet(_bufp, _hdrs, 0));
  }
  HeaderIterator
  headerEnd()
  {
    return HeaderIterator(_bufp, _hdrs, TS_NULL_MLOC);
  }
  TSMBuffer _bufp;
  TSMLoc _hdrs;
  TSMLoc _url;
};

/* S3 auth v4 utility API */

static const String X_AMZ_CONTENT_SHA256 = "x-amz-content-sha256";
static const String X_AMX_DATE           = "x-amz-date";
static const String X_AMZ                = "x-amz-";
static const String CONTENT_TYPE         = "content-type";
static const String HOST                 = "host";

String trimWhiteSpaces(const String &s);

template <typename ContainerType>
void
commaSeparateString(ContainerType &ss, const String &input, bool trim = true, bool lowerCase = true)
{
  std::istringstream istr(input);
  String token;

  while (std::getline(istr, token, ',')) {
    token = trim ? trimWhiteSpaces(token) : token;
    if (lowerCase) {
      std::transform(token.begin(), token.end(), token.begin(), ::tolower);
    }
    ss.insert(ss.end(), token);
  }
}

class AwsAuthV4
{
public:
  AwsAuthV4(TsInterface &api, time_t *now, bool signPayload, const char *awsAccessKeyId, size_t awsAccessKeyIdLen,
            const char *awsSecretAccessKey, size_t awsSecretAccessKeyLen, const char *awsService, size_t awsServiceLen,
            const StringSet &includedHeaders, const StringSet &excludedHeaders, const StringMap &regionMap);
  const char *getDateTime(size_t *dateTimeLen);
  String getPayloadHash();
  String getAuthorizationHeader();

private:
  TsInterface &_api;
  char _dateTime[sizeof "20170428T010203Z"];
  bool _signPayload               = false;
  const char *_awsAccessKeyId     = nullptr;
  size_t _awsAccessKeyIdLen       = 0;
  const char *_awsSecretAccessKey = nullptr;
  size_t _awsSecretAccessKeyLen   = 0;
  const char *_awsService         = nullptr;
  size_t _awsServiceLen           = 0;

  const StringSet &_includedHeaders;
  const StringSet &_excludedHeaders;
  const StringMap &_regionMap;
};
#endif /* PLUGINS_S3_AUTH_AWS_AUTH_V4_CC_ */
