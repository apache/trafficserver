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
//////////////////////////////////////////////////////////////////////////////////////////////
// statement.cc: Implementation of the statement base class.
//
//
#include "statement.h"

void
Statement::append(Statement *stmt)
{
  Statement *tmp = this;

  TSReleaseAssert(stmt->_next == nullptr);
  while (tmp->_next) {
    tmp = tmp->_next;
  }
  tmp->_next = stmt;
}

ResourceIDs
Statement::get_resource_ids() const
{
  const Statement *stmt = this;
  ResourceIDs      ids  = RSRC_NONE;

  while (stmt) {
    ids  = static_cast<ResourceIDs>(ids | stmt->_rsrc);
    stmt = stmt->_next;
  }

  return ids;
}

bool
Statement::set_hook(TSHttpHookID hook)
{
  bool ret = std::find(_allowed_hooks.begin(), _allowed_hooks.end(), hook) != _allowed_hooks.end();

  if (ret) {
    _hook = hook;
  }

  return ret;
}

// This should be overridden for any Statement which only supports some hooks
void
Statement::initialize_hooks()
{
  add_allowed_hook(TS_HTTP_READ_RESPONSE_HDR_HOOK);
  add_allowed_hook(TS_HTTP_PRE_REMAP_HOOK);
  add_allowed_hook(TS_HTTP_READ_REQUEST_HDR_HOOK);
  add_allowed_hook(TS_HTTP_SEND_REQUEST_HDR_HOOK);
  add_allowed_hook(TS_HTTP_SEND_RESPONSE_HDR_HOOK);
  add_allowed_hook(TS_REMAP_PSEUDO_HOOK);
  add_allowed_hook(TS_HTTP_TXN_START_HOOK);
  add_allowed_hook(TS_HTTP_TXN_CLOSE_HOOK);
}

void
Statement::acquire_txn_slot()
{
  // Don't do anything if we don't need it
  if (!need_txn_slot() || _txn_slot >= 0) {
    return;
  }

  // Only call the index reservation once per plugin load
  static int txn_slot_index = []() -> int {
    int index = -1;

    if (TS_ERROR == TSUserArgIndexReserve(TS_USER_ARGS_TXN, PLUGIN_NAME, "HRW txn variables", &index)) {
      TSError("[%s] failed to reserve user arg index", PLUGIN_NAME);
      return -1; // Fallback value
    }
    return index;
  }();

  _txn_slot = txn_slot_index;
}

void
Statement::acquire_txn_private_slot()
{
  // Don't do anything if we don't need it
  if (!need_txn_private_slot() || _txn_private_slot >= 0) {
    return;
  }

  // Only call the index reservation once per plugin load
  static int txn_private_slot_index = []() -> int {
    int index = -1;

    if (TS_ERROR == TSUserArgIndexReserve(TS_USER_ARGS_TXN, PLUGIN_NAME, "HRW txn private variables", &index)) {
      TSError("[%s] failed to reserve user arg index", PLUGIN_NAME);
      return -1; // Fallback value
    }
    return index;
  }();

  _txn_private_slot = txn_private_slot_index;
}

// Parse NextHop qualifiers
NextHopQualifiers
Statement::parse_next_hop_qualifier(const std::string &q) const
{
  NextHopQualifiers qual = NEXT_HOP_NONE;

  if (q == "HOST") {
    qual = NEXT_HOP_HOST;
  } else if (q == "PORT") {
    qual = NEXT_HOP_PORT;
  } else {
    TSError("[%s] Invalid NextHop() qualifier: %s", PLUGIN_NAME, q.c_str());
  }

  return qual;
}

// Parse URL qualifiers, this one is special since it's used in a few places.
UrlQualifiers
Statement::parse_url_qualifier(const std::string &q) const
{
  UrlQualifiers qual = URL_QUAL_NONE;

  if (q == "HOST") {
    qual = URL_QUAL_HOST;
  } else if (q == "PORT") {
    qual = URL_QUAL_PORT;
  } else if (q == "PATH") {
    qual = URL_QUAL_PATH;
  } else if (q == "QUERY") {
    qual = URL_QUAL_QUERY;
  } else if (q == "SCHEME") {
    qual = URL_QUAL_SCHEME;
  } else if (q == "URL") {
    qual = URL_QUAL_URL;
  } else {
    TSError("[%s] Invalid URL() qualifier: %s", PLUGIN_NAME, q.c_str());
  }

  return qual;
}

// Parse HTTP CNTL qualifiers
TSHttpCntlType
Statement::parse_http_cntl_qualifier(const std::string &q) const
{
  TSHttpCntlType qual = TS_HTTP_CNTL_LOGGING_MODE;

  if (q == "LOGGING") {
    qual = TS_HTTP_CNTL_LOGGING_MODE;
  } else if (q == "INTERCEPT_RETRY") {
    qual = TS_HTTP_CNTL_INTERCEPT_RETRY_MODE;
  } else if (q == "RESP_CACHEABLE") {
    qual = TS_HTTP_CNTL_RESPONSE_CACHEABLE;
  } else if (q == "REQ_CACHEABLE") {
    qual = TS_HTTP_CNTL_REQUEST_CACHEABLE;
  } else if (q == "SERVER_NO_STORE") {
    qual = TS_HTTP_CNTL_SERVER_NO_STORE;
  } else if (q == "TXN_DEBUG") {
    qual = TS_HTTP_CNTL_TXN_DEBUG;
  } else if (q == "SKIP_REMAP") {
    qual = TS_HTTP_CNTL_SKIP_REMAPPING;
  } else {
    TSError("[%s] Invalid HTTP-CNTL() qualifier: %s", PLUGIN_NAME, q.c_str());
  }

  return qual;
}
