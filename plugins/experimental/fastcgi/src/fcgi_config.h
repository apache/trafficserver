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

#include <list>
#include <map>
#include <stdint.h>
#include <string>
#include <ts/ts.h>
#include <utility>

#define PLUGIN_NAME "ats_fastcgi"
#define PLUGIN_VENDOR "Apache Software Foundation"
#define PLUGIN_SUPPORT "dev@trafficserver.apache.org"
#pragma once
namespace ats_plugin
{
typedef enum {
  fcgiEnabled,
  fcgiHostname,
  fcgiServerIp,
  fcgiServerPort,
  fcgiInclude,
  fcgiDocumentRoot,
  fcgiHtml,
  fcgiMinConnections,
  fcgiMaxConnections,
  fcgiMaxRequests,
  fcgiRequestQueueSize
} FcgiConfigKey;
typedef enum {
  gatewayInterface,
  serverSoftware,
  queryString,
  requestMethod,
  contentType,
  contentLength,
  scriptFilename,
  scriptName,
  requestUri,
  documentUri,
  documentRoot,
  serverProtocol,
  remoteAddr,
  remotePort,
  serverAddr,
  serverPort,
  serverName
} FcgiParamKey;

typedef std::map<uint32_t, int8_t> UintMap;
typedef std::map<std::string, std::string> FCGIParams;

class FcgiPluginConfig
{
  bool enabled;
  TSMgmtString hostname;
  TSMgmtString server_ip;
  TSMgmtString server_port;
  TSMgmtString include;
  FCGIParams *params;
  TSMgmtString document_root;
  TSMgmtString html;
  TSMgmtInt min_connections, max_connections, max_requests, request_queue_size;

public:
  FcgiPluginConfig()
    : enabled(true),
      hostname(nullptr),
      server_ip(nullptr),
      server_port(nullptr),
      include(nullptr),
      params(nullptr),
      document_root(nullptr),
      html(nullptr),
      min_connections(0),
      max_connections(0),
      max_requests(0),
      request_queue_size(0)
  {
  }

  ~FcgiPluginConfig()
  {
    hostname           = nullptr;
    server_ip          = nullptr;
    server_port        = nullptr;
    include            = nullptr;
    document_root      = nullptr;
    html               = nullptr;
    min_connections    = 0;
    max_connections    = 0;
    max_requests       = 0;
    request_queue_size = 0;
  }

  FcgiPluginConfig *initConfig(const char *fn);
  bool getFcgiEnabledStatus();
  void setFcgiEnabledStatus(bool val);

  TSMgmtString getHostname();
  void setHostname(char *str);
  TSMgmtString getServerIp();
  void setServerIp(char *str);
  TSMgmtString getServerPort();
  void setServerPort(char *str);
  TSMgmtString getIncludeFilePath();
  void setIncludeFilePath(char *str);
  FCGIParams *getFcgiParams();
  void setFcgiParams(FCGIParams *params);
  TSMgmtString getDocumentRootDir();
  void setDocumentRootDir(char *str);
  TSMgmtString getHtml();
  void setHtml(char *str);
  TSMgmtInt getMinConnLength();
  void setMinConnLength(int64_t minLen);
  TSMgmtInt getMaxConnLength();
  void setMaxConnLength(int64_t maxLen);
  TSMgmtInt getMaxReqLength();
  void setMaxReqLength(int64_t maxLen);
  TSMgmtInt getRequestQueueSize();
  void setRequestQueueSize(int64_t queueSize);
};

class InterceptPluginData
{
  UintMap *active_hash_map;
  TSMutex mutex;
  uint64_t seq_id;
  int txn_slot;
  FcgiPluginConfig *global_config;
  TSHRTime last_gc_time;
  bool read_while_writer;
  int tol_global_hook_reqs;
  int tol_remap_hook_reqs;
  int tol_non_cacheable_reqs;
  int tol_got_passed_reqs;

public:
  InterceptPluginData()
    : active_hash_map(nullptr),
      mutex(0),
      seq_id(0),
      txn_slot(0),
      global_config(nullptr),
      last_gc_time(0),
      read_while_writer(0),
      tol_global_hook_reqs(0),
      tol_remap_hook_reqs(0),
      tol_non_cacheable_reqs(0),
      tol_got_passed_reqs(0){
        // TSDebug(PLUGIN_NAME, "FCGIPluginData Initialised.");
      };
  ~InterceptPluginData();
  FcgiPluginConfig *getGlobalConfigObj();
  void setGlobalConfigObj(FcgiPluginConfig *config);
};
} // namespace ats_plugin
