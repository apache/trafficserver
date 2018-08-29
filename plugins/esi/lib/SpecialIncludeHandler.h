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

#pragma once

#include "HttpDataFetcher.h"
#include "Variables.h"
#include "Expression.h"

namespace EsiLib
{
class SpecialIncludeHandler
{
public:
  SpecialIncludeHandler(Variables &esi_vars, Expression &esi_expr, HttpDataFetcher &http_fetcher)
    : _esi_vars(esi_vars), _esi_expr(esi_expr), _http_fetcher(http_fetcher)
  {
  }

  virtual int handleInclude(const char *data, int data_len) = 0;

  virtual void handleParseComplete() = 0;

  /** trivial implementation */
  virtual DataStatus
  getIncludeStatus(int include_id)
  {
    const char *data;
    int data_len;
    return getData(include_id, data, data_len) ? STATUS_DATA_AVAILABLE : STATUS_ERROR;
  }

  virtual bool getData(int include_id, const char *&data, int &data_len) = 0;

  virtual void
  getFooter(const char *&footer, int &footer_len)
  {
    footer     = nullptr;
    footer_len = 0;
  }

  virtual ~SpecialIncludeHandler(){};

protected:
  Variables &_esi_vars;
  Expression &_esi_expr;
  HttpDataFetcher &_http_fetcher;
};
}; // namespace EsiLib
