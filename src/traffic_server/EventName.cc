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

#include "tscore/ink_config.h"
#include <cstdio>
#include <cstring>

#include "iocore/eventsystem/P_EventSystem.h"
#include "iocore/cache/Cache.h"
#include "iocore/net/Net.h"
#include "iocore/hostdb/HostDB.h"
#include "iocore/hostdb/P_RefCountCache.h"

/*-------------------------------------------------------------------------
  event_int_to_string

  This routine will translate an integer event number to a string
  identifier based on a brute-force search of a switch tag.  If the event
  cannot be located in the switch table, the routine will construct and
  return a string of the integer identifier.
  -------------------------------------------------------------------------*/

const char *
event_int_to_string(int event, int blen, char *buffer)
{
  return "UNKNOWN_EVENT";
}
