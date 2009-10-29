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


const char *
INKStrEvent(INKEvent e)
{
  switch (e) {
#define EVENT(a) case a: return #a

    EVENT(INK_EVENT_NONE);
    EVENT(INK_EVENT_IMMEDIATE);
    EVENT(INK_EVENT_TIMEOUT);
    EVENT(INK_EVENT_ERROR);
    EVENT(INK_EVENT_CONTINUE);

    EVENT(INK_EVENT_VCONN_READ_READY);
    EVENT(INK_EVENT_VCONN_WRITE_READY);
    EVENT(INK_EVENT_VCONN_READ_COMPLETE);
    EVENT(INK_EVENT_VCONN_WRITE_COMPLETE);
    EVENT(INK_EVENT_VCONN_EOS);

    EVENT(INK_EVENT_NET_CONNECT);
    EVENT(INK_EVENT_NET_CONNECT_FAILED);
    EVENT(INK_EVENT_NET_ACCEPT);
    EVENT(INK_EVENT_NET_ACCEPT_FAILED);

    EVENT(INK_EVENT_CACHE_OPEN_READ);
    EVENT(INK_EVENT_CACHE_OPEN_READ_FAILED);
    EVENT(INK_EVENT_CACHE_OPEN_WRITE);
    EVENT(INK_EVENT_CACHE_OPEN_WRITE_FAILED);
  default:
    return "UNKOWN EVENT";
  }
}
