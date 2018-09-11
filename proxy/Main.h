/** @file

  This is the primary include file for the proxy cache system.

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

#include "tscore/ink_platform.h"
#include "tscore/ink_apidefs.h"
#include "tscore/ink_defs.h"
#include "tscore/Regression.h"
#include "tscore/I_Version.h"

//
// Constants
//
#define ET_CACHE ET_CALL

// TODO: consolidate location of these defaults
#define DEFAULT_ROOT_DIRECTORY PREFIX
#define DEFAULT_LOCAL_STATE_DIRECTORY "var/trafficserver"
#define DEFAULT_SYSTEM_CONFIG_DIRECTORY "etc/trafficserver"
#define DEFAULT_LOG_DIRECTORY "var/log/trafficserver"
#define DEFAULT_BIND_STDOUT ""
#define DEFAULT_BIND_STDERR ""

//
// Global Data
//
// Global Configuration
extern int accept_till_done;
extern int http_accept_file_descriptor;
extern int command_flag;
extern int auto_clear_hostdb_flag;
extern int auto_clear_cache_flag;
extern int fds_limit;
extern int debug_level;

extern int remote_management_flag;

inkcoreapi extern int qt_accept_file_descriptor;

// Debugging Configuration
extern char debug_host[MAXDNAME + 1];
extern int debug_port;

// Default socket buffer limits
extern int default_sndbuf_size;
extern int default_rcvbuf_size;

//
// Functions
//
inline bool
maintainance_mode()
{
  return (command_flag ? true : false);
}

extern AppVersionInfo appVersionInfo;

void crash_logger_init(const char *user);
void crash_logger_invoke(int signo, siginfo_t *info, void *ctx);
