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
//
// Base class for all Conditions and Operations. We share the "linked" list, and the
// resource management / requirements.
//
#ifndef __STATEMENT_H__
#define __STATEMENT_H__ 1

#include <string>
#include <time.h>
#include <vector>

#include "ts/ts.h"

#include "resources.h"
#include "parser.h"
#include "lulu.h"

// URL data (both client and server)
enum UrlQualifiers {
  URL_QUAL_NONE,
  URL_QUAL_HOST,
  URL_QUAL_PORT,
  URL_QUAL_PATH,
  URL_QUAL_QUERY,
  URL_QUAL_MATRIX,
  URL_QUAL_SCHEME,
  URL_QUAL_URL
};

// NOW data
enum NowQualifiers {
  NOW_QUAL_EPOCH,
  NOW_QUAL_YEAR,
  NOW_QUAL_MONTH,
  NOW_QUAL_DAY,
  NOW_QUAL_HOUR,
  NOW_QUAL_MINUTE,
  NOW_QUAL_WEEKDAY,
  NOW_QUAL_YEARDAY
};

enum GeoQualifiers {
  GEO_QUAL_COUNTRY,
  GEO_QUAL_COUNTRY_ISO,
  GEO_QUAL_ASN,
  GEO_QUAL_ASN_NAME,
};

class Statement
{
public:
  Statement() : _next(NULL), _pdata(NULL), _rsrc(RSRC_NONE), _initialized(false), _hook(TS_HTTP_READ_RESPONSE_HDR_HOOK)
  {
    TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for Statement");
  }

  virtual ~Statement()
  {
    TSDebug(PLUGIN_NAME_DBG, "Calling DTOR for Statement");
    free_pdata();
  }

  // Private data
  void
  set_pdata(void *pdata)
  {
    _pdata = pdata;
  }
  void *
  get_pdata() const
  {
    return (_pdata);
  }
  virtual void
  free_pdata()
  {
    TSfree(_pdata);
    _pdata = NULL;
  }

  // Which hook are we adding this statement to?
  bool set_hook(TSHttpHookID hook);
  TSHttpHookID
  get_hook() const
  {
    return _hook;
  }

  // Which hooks are this "statement" applicable for? Used during parsing only.
  void
  add_allowed_hook(const TSHttpHookID hook)
  {
    _allowed_hooks.push_back(hook);
  }

  // Linked list.
  void append(Statement *stmt);

  ResourceIDs get_resource_ids() const;

  virtual void
  initialize(Parser &)
  { // Parser &p
    TSReleaseAssert(_initialized == false);
    initialize_hooks();
    _initialized = true;
  }
  bool
  initialized() const
  {
    return _initialized;
  }

protected:
  virtual void initialize_hooks();

  UrlQualifiers parse_url_qualifier(const std::string &q) const;

  void
  require_resources(const ResourceIDs ids)
  {
    _rsrc = static_cast<ResourceIDs>(_rsrc | ids);
  }

  Statement *_next; // Linked list

private:
  DISALLOW_COPY_AND_ASSIGN(Statement);

  void *_pdata;
  ResourceIDs _rsrc;
  bool _initialized;
  std::vector<TSHttpHookID> _allowed_hooks;
  TSHttpHookID _hook;
};

#endif // __STATEMENT_H
