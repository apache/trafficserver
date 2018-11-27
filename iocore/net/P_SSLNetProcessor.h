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

/****************************************************************************

  P_SSLNetProcessor.h

  The SSL version of the UnixNetProcessor class.  The majority of the logic
  is in UnixNetProcessor.  The SSLNetProcessor provides the following:

  * SSL library initialization through the start() method.
  * Allocation of a SSLNetVConnection through the allocate_vc virtual method.

  Possibly another pass through could simplify the allocate_vc logic too, but
  I think I will stop here for now.

 ****************************************************************************/

#pragma once

#include "tscore/ink_platform.h"
#include "P_Net.h"
#include "P_SSLConfig.h"
#include <openssl/ssl.h>

class UnixNetVConnection;
struct NetAccept;

//////////////////////////////////////////////////////////////////
//
//  class SSLNetProcessor
//
//////////////////////////////////////////////////////////////////
struct SSLNetProcessor : public UnixNetProcessor {
public:
  int start(int, size_t stacksize) override;

  void cleanup(void);

  SSLNetProcessor();
  ~SSLNetProcessor() override;

  //
  // Private
  //

  NetAccept *createNetAccept(const NetProcessor::AcceptOptions &opt) override;
  NetVConnection *allocate_vc(EThread *t) override;

  // noncopyable
  SSLNetProcessor(const SSLNetProcessor &) = delete;
  SSLNetProcessor &operator=(const SSLNetProcessor &) = delete;
};

extern SSLNetProcessor ssl_NetProcessor;
