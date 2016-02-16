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

#include <stdio.h>
#include "I_DNS.h"

#include "diags.i"

main()
{
  init_diags("net_test", NULL);
  ink_event_system_init(EVENT_SYSTEM_MODULE_VERSION);

  signal(SIGPIPE, SIG_IGN);
  eventProcessor.start(2);
  netProcessor.start();

  printf("hello world\n");

  dnsProcessor.start();
  this_ethread()->execute();
}
