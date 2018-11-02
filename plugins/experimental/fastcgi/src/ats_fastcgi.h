/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#define PLUGIN_VENDOR "Apache Software Foundation"
#define PLUGIN_SUPPORT "dev@trafficserver.apache.org"

#define ATS_MODULE_FCGI_NAME "ats_fastcgi"
#define ATS_MOD_FCGI_VERSION "ats_fastcgi"
#define ATS_FCGI_PROFILER true

#include "fcgi_config.h"
#include <atscppapi/GlobalPlugin.h>
#include "server.h"
#if ATS_FCGI_PROFILER
#include "Profiler.h"
#endif

using namespace atscppapi;

class FCGIServer;

namespace InterceptGlobal
{
extern GlobalPlugin *plugin;
extern ats_plugin::InterceptPluginData *plugin_data;
// TODO (Rakesh): Move to FCGI connect file
extern ats_plugin::Server *gServer;
extern thread_local pthread_key_t threadKey;
extern int reqBegId, reqEndId, respBegId, respEndId, threadCount, phpConnCount;
#ifdef ATS_FCGI_PROFILER
extern ats_plugin::Profiler profiler;
#endif
} // namespace InterceptGlobal
