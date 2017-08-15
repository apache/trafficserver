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

 */

/****************************************************************************

  P_QUICNetProcessor.h

  The QUIC version of the UnixNetProcessor class.  The majority of the logic
  is in UnixNetProcessor.  The QUICNetProcessor provides the following:

  * QUIC library initialization through the start() method.
  * Allocation of a QUICNetVConnection through the allocate_vc virtual method.

  Possibly another pass through could simplify the allocate_vc logic too, but
  I think I will stop here for now.

 ****************************************************************************/
#pragma once

#include "ts/ink_platform.h"
#include "P_Net.h"
// #include "P_QUICConfig.h"
#include "ts/Map.h"

class UnixNetVConnection;
struct NetAccept;

//////////////////////////////////////////////////////////////////
//
//  class QUICNetProcessor
//
//////////////////////////////////////////////////////////////////
class QUICNetProcessor : public UnixNetProcessor
{
public:
  QUICNetProcessor();
  virtual ~QUICNetProcessor();

  virtual int start(int, size_t stacksize) override;
  void cleanup();

  virtual NetAccept *createNetAccept(const NetProcessor::AcceptOptions &opt) override;
  virtual NetVConnection *allocate_vc(EThread *t) override;

  Action *main_accept(Continuation *cont, SOCKET fd, AcceptOptions const &opt) override;

private:
  QUICNetProcessor(const QUICNetProcessor &);
  QUICNetProcessor &operator=(const QUICNetProcessor &);

  SSL_CTX *_ssl_ctx;
};

extern QUICNetProcessor quic_NetProcessor;
