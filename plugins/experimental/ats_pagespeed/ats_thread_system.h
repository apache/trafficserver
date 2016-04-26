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

#ifndef ATS_THREAD_SYSTEM_H_
#define ATS_THREAD_SYSTEM_H_

#include <pthread.h>

#include <ts/ts.h>
#include "net/instaweb/system/public/system_thread_system.h"
#include "net/instaweb/util/public/thread.h"
#include "net/instaweb/util/public/thread_system.h"
#include "net/instaweb/util/public/pthread_rw_lock.h"
#include "net/instaweb/util/public/condvar.h"

namespace net_instaweb
{
class AtsThreadSystem : public net_instaweb::SystemThreadSystem
{
public:
  virtual void
  BeforeThreadRunHook()
  {
    TSThreadInit();
  }

  virtual ~AtsThreadSystem() {}
};

} // net_instaweb

#endif // ATS_THREAD_SYSTEM_H_
