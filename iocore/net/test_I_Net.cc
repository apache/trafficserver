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

#include "P_Net.h"
#include <netdb.h>

#include "diags.i"

/*
 * Choose a net test application
 */
//#include "NetTest-http-server.c"
#include "NetTest-simple-proxy.c"

int
main()
{
  // do not buffer stdout
  setbuf(stdout, nullptr);
  int nproc = ink_number_of_processors();

  RecModeT mode_type = RECM_STAND_ALONE;

  init_diags("net_test", nullptr);
  RecProcessInit(mode_type);
  ink_event_system_init(EVENT_SYSTEM_MODULE_PUBLIC_VERSION);
  ink_net_init(NET_SYSTEM_MODULE_PUBLIC_VERSION);

  /*
   * ignore broken pipe
   */
  signal(SIGPIPE, SIG_IGN);

  /*
   * start processors
   */

  eventProcessor.start(nproc);
  RecProcessStart();

  /*
   *  Reset necessary config variables
   */

#ifdef USE_SOCKS
  net_config_socks_server_host = "209.131.52.54";
  net_config_socks_server_port = 1080;
  net_config_socks_needed      = 1;
#endif

  netProcessor.start();
  sslNetProcessor.start(1);

  /*
   * Call the tests main function
   */
  test_main();
  this_thread()->execute();
  return 0;
}
