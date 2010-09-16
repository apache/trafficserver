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

#include <arpa/inet.h>

struct NetConnectTester:public Continuation
{

  NetConnectTester(ProxyMutex * _mutex):Continuation(_mutex)
  {
    SET_HANDLER(&NetConnectTester::handle_connect);
  }

  int handle_connect(int event, void *data)
  {
    switch (event) {
    case NET_EVENT_OPEN:{
        Debug("net_test", "Made a connection\n");
        NetVConnection *vc = (NetVConnection *) data;
        vc->do_io_close();
        break;
      }
    case NET_EVENT_OPEN_FAILED:
      Debug("net_test", "connec_s failed (%s)\n", get_net_error_name(-(int) data));
      break;
    default:
      ink_debug_assert(!"unknown event");
    }
    delete this;
    return EVENT_DONE;
  }
};



int
test_main()
{
  unsigned int srv_ip[4];
  unsigned int srv_port[4];

  // www.inktomi.com:80
  srv_ip[0] = inet_addr("209.131.63.207");
  srv_port[0] = 80;

  // a dead machine
  srv_ip[1] = inet_addr("209.131.39.251");
  srv_port[1] = 80;

  // npdev:80
  srv_ip[2] = inet_addr("209.131.48.213");
  srv_port[2] = 80;

  srv_ip[3] = inet_addr("209.131.39.251");
  srv_port[3] = 80;

  for (int i = 0; i < 3; i++) {
    NetConnectTester *nct = NEW(new NetConnectTester(new_ProxyMutex()));
    EThread *t = this_ethread();
    MUTEX_TRY_LOCK(lock, nct->mutex, t);
    ink_debug_assert(lock);
    Action *a = sslNetProcessor.connect_s(nct, srv_ip[i], srv_port[i], 0, 10 * 1000);
  }
  return 0;
}
