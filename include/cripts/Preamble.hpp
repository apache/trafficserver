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
// Having both of these for now, until we decide which we like better...
#define do_remap()           void _do_remap(cripts::Context *context)
#define do_post_remap()      void _do_post_remap(cripts::Context *context)
#define do_cache_lookup()    void _do_cache_lookup(cripts::Context *context)
#define do_send_request()    void _do_send_request(cripts::Context *context)
#define do_read_response()   void _do_read_response(cripts::Context *context)
#define do_send_response()   void _do_send_response(cripts::Context *context)
#define do_txn_close()       void _do_txn_close(cripts::Context *context)
#define do_init()            void _do_init(TSRemapInterface *)
#define do_create_instance() void _do_create_instance(cripts::InstanceContext *context)
#define do_delete_instance() void _do_delete_instance(cripts::InstanceContext *context)

#define DoRemap()          void _do_remap(cripts::Context *context)
#define DoPostRemap()      void _do_post_remap(cripts::Context *context)
#define DoCacheLookup()    void _do_cache_lookup(cripts::Context *context)
#define DoSendRequest()    void _do_send_request(cripts::Context *context)
#define DoReadResponse()   void _do_read_response(cripts::Context *context)
#define DoSendResponse()   void _do_send_response(cripts::Context *context)
#define DoTxnClose()       void _do_txn_close(cripts::Context *context)
#define DoInit()           void _do_init(TSRemapInterface *)
#define DoCreateInstance() void _do_create_instance(cripts::InstanceContext *context)
#define DoDeleteInstance() void _do_delete_instance(cripts::InstanceContext *context)

// For the global plugins
#define glb_txn_start()     void _glb_txn_start(cripts::Context *context)
#define glb_read_request()  void _glb_read_request(cripts::Context *context)
#define glb_pre_remap()     void _glb_pre_remap(cripts::Context *context)
#define glb_post_remap()    void _glb_post_remap(cripts::Context *context)
#define glb_cache_lookup()  void _glb_cache_lookup(cripts::Context *context)
#define glb_send_request()  void _glb_send_request(cripts::Context *context)
#define glb_read_response() void _glb_read_response(cripts::Context *context)
#define glb_send_response() void _glb_send_response(cripts::Context *context)
#define glb_txn_close()     void _glb_txn_close(cripts::Context *context)
#define glb_init()          void _glb_init(cripts::InstanceContext *context)

#define GlbTxnStart()     void _glb_txn_start(cripts::Context *context)
#define GlbReadRequest()  void _glb_read_request(cripts::Context *context)
#define GlbPreRemap()     void _glb_pre_remap(cripts::Context *context)
#define GlbPostRemap()    void _glb_post_remap(cripts::Context *context)
#define GlbSendRequest()  void _glb_send_request(cripts::Context *context)
#define GlbReadResponse() void _glb_read_response(cripts::Context *context)
#define GlbSendResponse() void _glb_send_response(cripts::Context *context)
#define GlbTxnClose()     void _glb_txn_close(cripts::Context *context)
#define GlbInit()         void _glb_init(cripts::InstanceContext *context)

#include "cripts/Headers.hpp"
#include "cripts/Urls.hpp"
#include "cripts/Configs.hpp"
#include "cripts/ConfigsBase.hpp"
#include "cripts/Connections.hpp"
#include "cripts/UUID.hpp"
#include "cripts/Matcher.hpp"
#include "cripts/Time.hpp"
#include "cripts/Crypto.hpp"
#include "cripts/Files.hpp"
#include "cripts/Metrics.hpp"
#include "cripts/Plugins.hpp"

// This is to make using these more convenient
using cripts::string;
using fmt::format;

// This needs to be last
#include "cripts/Context.hpp"

// These are globals, making certain operations nice and convenient, without wasting plugin space
extern cripts::Proxy    proxy;   // Access to all overridable configurations
extern cripts::Control  control; // Access to the HTTP control mechanism
extern cripts::Versions version; // Access to the ATS version information
