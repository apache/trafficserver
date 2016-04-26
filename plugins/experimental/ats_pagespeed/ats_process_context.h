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

#ifndef ATS_PROCESS_CONTEXT_H_
#define ATS_PROCESS_CONTEXT_H_

#include "net/instaweb/util/public/google_message_handler.h"
#include "net/instaweb/util/public/message_handler.h"
#include "net/instaweb/util/public/scoped_ptr.h"
#include "net/instaweb/rewriter/public/process_context.h"

namespace net_instaweb
{
class AtsRewriteDriverFactory;
class ProxyFetchFactory;
class AtsServerContext;

class AtsProcessContext : ProcessContext
{
public:
  explicit AtsProcessContext();
  virtual ~AtsProcessContext();

  // TODO(oschaaf): const correctness
  MessageHandler *
  message_handler()
  {
    return message_handler_.get();
  }
  AtsRewriteDriverFactory *
  driver_factory()
  {
    return driver_factory_.get();
  }
  ProxyFetchFactory *
  proxy_fetch_factory()
  {
    return proxy_fetch_factory_.get();
  }
  AtsServerContext *
  server_context()
  {
    return server_context_;
  }

private:
  scoped_ptr<MessageHandler> message_handler_;
  scoped_ptr<AtsRewriteDriverFactory> driver_factory_;
  scoped_ptr<ProxyFetchFactory> proxy_fetch_factory_;
  AtsServerContext *server_context_;
};

} // namespace net_instaweb

#endif // ATS_PROCESS_CONTEXT_H_
