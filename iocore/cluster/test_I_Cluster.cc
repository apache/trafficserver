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

/*
  This is a bogus test file because it does not need any cluster
  functions : the actual test file is in the cache directory.
  This file is a placeholder because each module is supposed to
  have a test file
  */

#include "P_Cluster.h"
#include <iostream.h>
#include <fstream.h>

#include "diags.i"

int
main(int argc, char *argv[])
{
  int i;
  int num_net_threads = ink_number_of_processors();
  init_diags("", NULL);
  RecProcessInit(RECM_STAND_ALONE);
  ink_event_system_init(EVENT_SYSTEM_MODULE_VERSION);
  eventProcessor.start(num_net_threads);
  RecProcessStart();
  // ink_aio_init(AIO_MODULE_VERSION);

  this_thread()->execute();
}
