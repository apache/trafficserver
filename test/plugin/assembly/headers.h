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

#ifndef _HEADERS_H_
#define _HEADERS_H_

#include "InkAPI.h"
#include "common.h"

int query_string_extract(TxnData * txn_data, char **query_store);
void query_and_cookies_extract(INKHttpTxn txnp, TxnData * txn_data, PairList * query, PairList * cookies);
int is_template_header(INKMBuffer bufp, INKMLoc hdr_loc);
int has_nocache_header(INKMLoc bufp, INKMLoc hdr_loc);
int request_looks_dynamic(INKMBuffer bufp, INKMLoc hdr_loc);
void modify_request_url(INKMBuffer bufp, INKMLoc url_loc, TxnData * txn_data);

#endif
