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

#ifndef _app_template_h_
#define _app_template_h_

#include "libts.h"

#define DEFAULT_ROOT_DIRECTORY            PREFIX
#define DEFAULT_LOCAL_STATE_DIRECTORY     "var/trafficserver"
#define DEFAULT_SYSTEM_CONFIG_DIRECTORY   "etc/trafficserver"
#define DEFAULT_LOG_DIRECTORY             "var/log/trafficserver"

#define DEFAULT_NUMBER_OF_THREADS         ink_number_of_processors()
#define DEFAULT_NUMBER_OF_UDP_THREADS     1


struct MyAccept : public Continuation {
  int accept_port;
  int main_event(int event, void *netvc);

  MyAccept()
    { SET_HANDLER(&MyAccept::main_event); }

};


#endif
