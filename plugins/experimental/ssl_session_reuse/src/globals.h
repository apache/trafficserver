/** @file

  globals.h - global default values

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

#include <string>
#include <vector>
#include <sstream>

const std::string cDefaultConfig("ats_ssl_session_reuse.xml");
const unsigned int cPubNumWorkerThreads(100);
const int cDefaultRedisPort(6379);
const std::string cDefaultRedisHost("localhost");
const std::string cDefaultRedisEndpoint("localhost:6379");
const unsigned int cDefaultRedisConnectTimeout(1000000);
const unsigned int cDefaultRedisPublishTries(5);
const unsigned int cDefaultRedisConnectTries(5);
const unsigned int cDefaultRedisRetryDelay(5000000);
const unsigned int cDefaultMaxQueuedMessages(1000);
const std::string cDefaultSubColoChannel("test.*");
const unsigned int cDefaultRedisMessageWaitTime(100000);
const unsigned int cDefaultMdbmCleanupTime(100);
const unsigned int cDefaultMdbmMaxSize(65536);
const unsigned int cDefaultMdbmPageSize(2048);
const int SUCCESS(0);
const int FAILURE(-1);
