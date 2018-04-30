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
 * @file aws_auth_v4_ts.h
 * @brief TS API adaptor and header iterator using the TS API which are swapped with mocks during testing.
 * @see aws_auth_v4.h
 */

#pragma once

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
  HeaderIterator &
  operator++()
  {
    /* @todo this is said to be slow in the API call comments, do something better here */
    TSMLoc next = TSMimeHdrFieldNext(_bufp, _hdrs, _field);
    TSHandleMLocRelease(_bufp, _hdrs, _field);
    _field = next;
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
  ~TsApi() override {}
  const char *
  getMethod(int *len) override
  {
    return TSHttpHdrMethodGet(_bufp, _hdrs, len);
  }
  const char *
  getHost(int *len) override
  {
    return TSHttpHdrHostGet(_bufp, _hdrs, len);
  }
  const char *
  getPath(int *len) override
  {
    return TSUrlPathGet(_bufp, _url, len);
  }
  const char *
  getQuery(int *len) override
  {
    return TSUrlHttpQueryGet(_bufp, _url, len);
  }
  HeaderIterator
  headerBegin() override
  {
    return HeaderIterator(_bufp, _hdrs, TSMimeHdrFieldGet(_bufp, _hdrs, 0));
  }
  HeaderIterator
  headerEnd() override
  {
    return HeaderIterator(_bufp, _hdrs, TS_NULL_MLOC);
  }
  TSMBuffer _bufp;
  TSMLoc _hdrs;
  TSMLoc _url;
};
