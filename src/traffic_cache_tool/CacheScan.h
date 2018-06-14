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
/*
 * File:   CacheScan.h
 * Author: persia
 *
 * Created on April 4, 2018, 10:06 AM
 */

#ifndef CACHESCAN_H
#define CACHESCAN_H
#include <thread>
#include <unordered_map>
#include "CacheDefs.h"

#include "../../proxy/hdrs/HTTP.h"
// using namespace ct;
namespace ct
{
class CacheScan
{
  Stripe *stripe;
  url_matcher *u_matcher;

public:
  CacheScan(Stripe *str, ts::FilePath const &path) : stripe(str)
  {
    if (path.has_path()) {
      u_matcher = new url_matcher(path);
    }
  };
  CacheScan(Stripe *str) : stripe(str) {}
  Errata Scan(bool search = false);
  Errata get_alternates(const char *buf, int length, bool search);
  int unmarshal(HdrHeap *hh, int buf_length, int obj_type, HdrHeapObjImpl **found_obj, RefCountObj *block_ref);
  Errata unmarshal(char *buf, int len, RefCountObj *block_ref);
  Errata unmarshal(HTTPHdrImpl *obj, intptr_t offset);
  Errata unmarshal(URLImpl *obj, intptr_t offset);
  Errata unmarshal(MIMEFieldBlockImpl *mf, intptr_t offset);
  Errata unmarshal(MIMEHdrImpl *obj, intptr_t offset);
  bool check_url(ts::MemSpan &mem, URLImpl *url);
};
} // namespace ct

#endif /* CACHESCAN_H */
