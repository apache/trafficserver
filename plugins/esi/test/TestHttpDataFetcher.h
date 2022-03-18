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

#include <string>

#include "HttpDataFetcher.h"

class TestHttpDataFetcher : public HttpDataFetcher
{
public:
  TestHttpDataFetcher() {}
  bool
  addFetchRequest(const std::string &url, FetchedDataProcessor *callback_obj = nullptr) override
  {
    ++_n_pending_requests;
    return true;
  }

  DataStatus
  getRequestStatus(const std::string &url) const override
  {
    if (_return_data) {
      return STATUS_DATA_AVAILABLE;
    }
    --(const_cast<int &>(_n_pending_requests));
    return STATUS_ERROR;
  }

  int
  getNumPendingRequests() const override
  {
    return _n_pending_requests;
  };

  bool
  getContent(const std::string &url, const char *&content, int &content_len) const override
  {
    TestHttpDataFetcher &curr_obj = const_cast<TestHttpDataFetcher &>(*this);
    --curr_obj._n_pending_requests;
    if (_return_data) {
      curr_obj._data.clear();
      curr_obj._data.append(">>>>> Content for URL [");
      curr_obj._data.append(url);
      curr_obj._data.append("] <<<<<");
      content     = curr_obj._data.data();
      content_len = curr_obj._data.size();
      return true;
    }
    return false;
  }

  void
  setReturnData(bool rd)
  {
    _return_data = rd;
  };

  bool
  getReturnData() const
  {
    return _return_data;
  };

private:
  int _n_pending_requests = 0;
  std::string _data;
  bool _return_data = true;
};
