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
#pragma once

// These are technically not needed, but useful to have around for advanced
// Cripters.
#include <algorithm>
#include <vector>
#include <string>
#include <string_view>
#include <chrono>
#include <climits>
#include <iostream> // Useful for debugging

#include <fmt/core.h>

#include "ts/ts.h"
#include "ts/remap.h"

#include "cripts/Lulu.hpp"
#include "cripts/Instance.hpp"
#include "cripts/Error.hpp"
#include "cripts/Transaction.hpp"

// This makes it nice and clean when the user of the framework defines the handlers in the cript.
#define do_remap()           void _do_remap(Cript::Context *context)
#define do_post_remap()      void _do_post_remap(Cript::Context *context)
#define do_send_response()   void _do_send_response(Cript::Context *context)
#define do_cache_lookup()    void _do_cache_lookup(Cript::Context *context)
#define do_send_request()    void _do_send_request(Cript::Context *context)
#define do_read_response()   void _do_read_response(Cript::Context *context)
#define do_txn_close()       void _do_txn_close(Cript::Context *context)
#define do_init()            void _do_init(TSRemapInterface *api_info)
#define do_create_instance() void _do_create_instance(Cript::InstanceContext *context)
#define do_delete_instance() void _do_delete_instance(Cript::InstanceContext *context)

#include "cripts/Headers.hpp"
#include "cripts/Urls.hpp"
#include "cripts/Configs.hpp"
#include "cripts/Connections.hpp"
#include "cripts/UUID.hpp"
#include "cripts/Matcher.hpp"
#include "cripts/Time.hpp"
#include "cripts/Crypto.hpp"
#include "cripts/Files.hpp"
#include "cripts/Metrics.hpp"
#include "cripts/Plugins.hpp"

// This is to make using these more convenient
using Cript::string;
using fmt::format;

// This needs to be last
#include "cripts/Context.hpp"

// These are globals, making certain operations nice and convenient, without wasting plugin space
extern Proxy    proxy;   // Access to all overridable configurations
extern Control  control; // Access to the HTTP control mechanism
extern Versions version; // Access to the ATS version information
