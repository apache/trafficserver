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

#ifndef _MAIN_H_
#define _MAIN_H_

#include "FileManager.h"
#include "WebOverview.h"
#include "I_Version.h"

// TODO: consolidate location of these defaults
#define DEFAULT_ROOT_DIRECTORY            PREFIX
#define DEFAULT_LOCAL_STATE_DIRECTORY     "var/trafficserver"
#define DEFAULT_SYSTEM_CONFIG_DIRECTORY   "etc/trafficserver"
#define DEFAULT_LOG_DIRECTORY             "var/log/trafficserver"

void MgmtShutdown(int status);
void fileUpdated(char *fname, bool incVersion);
void runAsUser(char *userName);
void printUsage(void);

extern FileManager *configFiles;
extern overviewPage *overviewGenerator;
extern AppVersionInfo appVersionInfo;

// Global strings
extern char mgmt_path[];

// Global variable to replace ifdef MGMT_LAUNCH_PROXY so that
// we can turn on/off proxy launch at runtime to facilitate
// manager only testing.
extern bool mgmt_launch_proxy;

#endif /* _MAIN_H_ */
