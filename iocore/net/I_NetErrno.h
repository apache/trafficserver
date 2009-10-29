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

#ifndef __I_NetErrno_h__
#define __I_NetErrno_h__

// Net error codes
#define ENET_THROTTLING                   (NET_ERRNO+1)
#define ENET_CONNECT_TIMEOUT              (NET_ERRNO+2)
#define ENET_CONNECT_FAILED               (NET_ERRNO+3)


//Socks error codes

#define ESOCK_DENIED                      (SOCK_ERRNO+0)
#define ESOCK_TIMEOUT                     (SOCK_ERRNO+1)
#define ESOCK_NO_SOCK_SERVER_CONN         (SOCK_ERRNO+2)



inline const char *
get_net_error_name(int err_no)
{
  switch (err_no) {
  case ENET_THROTTLING:
    return "ENET_THROTTLING";
  case ENET_CONNECT_TIMEOUT:
    return "ENET_CONNECT_TIMEOUT";
  case ENET_CONNECT_FAILED:
    return "ENET_CONNECT_FAILED";

  case ESOCK_DENIED:
    return "ESOCK_DENIED";
  case ESOCK_TIMEOUT:
    return "ESOCK_TIMEOUT";
  case ESOCK_NO_SOCK_SERVER_CONN:
    return "ESOCK_NO_SOCK_SERVER_CONN";

  default:
    return "UNKNOWN ERROR";
  }
}

#endif
