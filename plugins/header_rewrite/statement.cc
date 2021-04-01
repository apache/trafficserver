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
  ResourceIDs ids       = RSRC_NONE;

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
  } else if (q == "MATRIX") {
    qual = URL_QUAL_MATRIX;
  } else if (q == "SCHEME") {
    qual = URL_QUAL_SCHEME;
  } else if (q == "URL") {
    qual = URL_QUAL_URL;
  } else {
    TSError("[%s] Invalid URL() qualifier: %s", PLUGIN_NAME, q.c_str());
  }

  return qual;
}
