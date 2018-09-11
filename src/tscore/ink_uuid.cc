/** @file
 *
 *  Basic implementation of RFC 4122, see
 *      https://www.ietf.org/rfc/rfc4122.txt
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */
#include <openssl/rand.h>

#include "tscore/ink_error.h"
#include "tscore/ink_uuid.h"

void
ATSUuid::initialize(TSUuidVersion v)
{
  switch (v) {
  case TS_UUID_UNDEFINED:
    ink_abort("Don't initialize to undefined UUID variant!");
    break;
  case TS_UUID_V1:
  case TS_UUID_V2:
  case TS_UUID_V3:
  case TS_UUID_V5:
    ink_zero(_uuid.data); // Not properly implemented yet, so set it to the Nil UUID
    break;
  case TS_UUID_V4:
    RAND_bytes(_uuid.data, sizeof(_uuid.data));
    _uuid.clockSeqAndReserved = (uint8_t)((_uuid.clockSeqAndReserved & 0x3F) | 0x80);
    _uuid.timeHighAndVersion  = (uint16_t)((_uuid.timeHighAndVersion & 0x0FFF) | 0x4000);

    break;
  }

  _version = _toString(_string) ? v : TS_UUID_UNDEFINED;
}

// Copy assignment
ATSUuid &
ATSUuid::operator=(const ATSUuid other)
{
  memcpy(_uuid.data, other._uuid.data, sizeof(_uuid.data));
  memcpy(_string, other._string, sizeof(_string));
  _version = other._version;

  return *this;
}

bool
ATSUuid::parseString(const char *str)
{
  int cnt = sscanf(str, "%08x-%04hx-%04hx-%02hhx%02hhx-%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx", &_uuid.timeLow, &_uuid.timeMid,
                   &_uuid.timeHighAndVersion, &_uuid.clockSeqAndReserved, &_uuid.clockSeqLow, &_uuid.node[0], &_uuid.node[1],
                   &_uuid.node[2], &_uuid.node[3], &_uuid.node[4], &_uuid.node[5]);

  if ((11 == cnt) && _toString(_string)) {
    switch (_uuid.timeHighAndVersion >> 12) {
    case 1:
      _version = TS_UUID_V1;
      break;
    case 2:
      _version = TS_UUID_V2;
      break;
    case 3:
      _version = TS_UUID_V3;
      break;
    case 4:
      _version = TS_UUID_V4;
      break;
    case 5:
      _version = TS_UUID_V5;
      break;
    default:
      _version = TS_UUID_UNDEFINED;
      break;
    }
  } else {
    _version = TS_UUID_UNDEFINED;
  }

  return (TS_UUID_UNDEFINED != _version);
}
