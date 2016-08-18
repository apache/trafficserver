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

#include "ats_server_context.h"
#include "ats_rewrite_driver_factory.h"
#include "ats_rewrite_options.h"

#include "net/instaweb/system/public/system_caches.h"

using namespace net_instaweb;

AtsRewriteOptions *
AtsServerContext::config()
{
  return (AtsRewriteOptions *)global_options();
}

AtsServerContext::AtsServerContext(AtsRewriteDriverFactory *factory)
  : // TODO(oschaaf): host/port
    SystemServerContext(factory, "foo.com" /*hostname*/, 8080 /*port*/),
    initialized_(false),
    ats_factory_(factory)
{
}

AtsServerContext::~AtsServerContext()
{
}
