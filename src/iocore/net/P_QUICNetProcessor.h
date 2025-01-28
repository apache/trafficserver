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

#pragma once

#include "P_UnixNetProcessor.h"

/****************************************************************************

  QUICNetProcessor.h

  The Quiche version of the UnixNetProcessor class.  The majority of the logic
  is in UnixNetProcessor.  QUICNetProcessor provides the following:

  * QUIC library initialization through the start() method.
  * Allocation of a QUICNetVConnection through the allocate_vc virtual method.

  Possibly another pass through could simplify the allocate_vc logic too, but
  I think I will stop here for now.

 ****************************************************************************/
#pragma once

#include "P_UnixNetProcessor.h"
#include "iocore/net/quic/QUICConnectionTable.h"
#include <quiche.h>

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

  void init() override;
  int  start(int, size_t stacksize) override;

  Action *connect_re(Continuation *cont, sockaddr const *addr, NetVCOptions const &opts) override;

  NetVConnection *allocate_vc(EThread *t) override;

  Action *main_accept(Continuation *cont, SOCKET fd, AcceptOptions const &opt) override;

  off_t quicPollCont_offset;

protected:
  NetAccept *createNetAccept(const NetProcessor::AcceptOptions &opt) override;

private:
  QUICNetProcessor(const QUICNetProcessor &);
  QUICNetProcessor &operator=(const QUICNetProcessor &);

  QUICConnectionTable *_ctable        = nullptr;
  quiche_config       *_quiche_config = nullptr;
};

extern QUICNetProcessor quic_NetProcessor;
