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

/****************************************************************************

  NetVConnection.cc

  This file implements an I/O Processor for network I/O.


 ****************************************************************************/

#include "P_Net.h"
#include "ts/apidefs.h"

////
// NetVConnection
//
Action *
NetVConnection::send_OOB(Continuation *, char *, int)
{
  return ACTION_RESULT_DONE;
}

void
NetVConnection::cancel_OOB()
{
  return;
}

/**
   PROXY Protocol check with IOBufferReader

   If the buffer has PROXY Protocol, it will be consumed by this function.
 */
bool
NetVConnection::has_proxy_protocol(IOBufferReader *reader)
{
  char buf[PPv1_CONNECTION_HEADER_LEN_MAX + 1];
  ts::TextView tv;
  tv.assign(buf, reader->memcpy(buf, sizeof(buf), 0));

  size_t len = proxy_protocol_parse(&this->pp_info, tv);

  if (len > 0) {
    reader->consume(len);
    return true;
  }

  return false;
}

/**
   PROXY Protocol check with buffer

   If the buffer has PROXY Protocol, it will be consumed by this function.
 */
bool
NetVConnection::has_proxy_protocol(char *buffer, int64_t *bytes_r)
{
  ts::TextView tv;
  tv.assign(buffer, *bytes_r);

  size_t len = proxy_protocol_parse(&this->pp_info, tv);

  if (len <= 0) {
    *bytes_r = -EAGAIN;
    return false;
  }

  *bytes_r -= len;
  if (*bytes_r <= 0) {
    *bytes_r = -EAGAIN;
  } else {
    Debug("ssl", "Moving %" PRId64 " characters remaining in the buffer from %p to %p", *bytes_r, buffer + len, buffer);
    memmove(buffer, buffer + len, *bytes_r);
  }

  return true;
}

////
// NetVCOptions
//
std::string_view
NetVCOptions::get_proto_string() const
{
  switch (ip_proto) {
  case USE_TCP:
    return IP_PROTO_TAG_TCP;
  case USE_UDP:
    return IP_PROTO_TAG_UDP;
  default:
    break;
  }
  return {};
}

std::string_view
NetVCOptions::get_family_string() const
{
  switch (ip_family) {
  case AF_INET:
    return IP_PROTO_TAG_IPV4;
  case AF_INET6:
    return IP_PROTO_TAG_IPV6;
  default:
    break;
  }
  return {};
}
