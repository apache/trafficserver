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
#pragma once

#include <cstdio>

#include "ts/apidefs.h"
#include "tscore/ink_memory.h"

// This is the C++ portions of things, which will need to get wrapped in C-helper APIs.
class ATSUuid
{
public:
  // Constructors
  ATSUuid() : _version(TS_UUID_UNDEFINED) {}
  ATSUuid &operator=(const ATSUuid other);

  ATSUuid(ATSUuid const &that) = default;

  // Initialize the UUID from a string
  bool parseString(const char *str);

  // Initialize a UUID using appropriate logic for the version specified. This can be done multiple times.
  void initialize(TSUuidVersion v);

  // These return the internal string representation of the UUID, do not mess with this string. There is
  // no transfer of ownership here. You will have to make a copy to take ownership!
  const char *
  getString() const
  {
    return valid() ? _string : nullptr;
  }

  const char *
  c_str() const
  {
    return getString();
  }

  TSUuidVersion
  version() const
  {
    return _version;
  }

  bool
  valid() const
  {
    return (TS_UUID_UNDEFINED != _version);
  }

  // Getter's for the various UUID components.
  uint32_t
  getTimeLow() const
  {
    return _uuid.timeLow;
  }

  uint16_t
  getTimeMid() const
  {
    return _uuid.timeMid;
  }

  uint16_t
  getTimeHighAndVersion() const
  {
    return _uuid.timeHighAndVersion;
  }

  uint8_t
  getClockSeqAndReserved() const
  {
    return _uuid.clockSeqAndReserved;
  }

  uint8_t
  getClockSeqLow() const
  {
    return _uuid.clockSeqLow;
  }

  const uint8_t *
  getNode() const
  {
    return _uuid.node;
  }

private:
  // This is the union of the raw data (128 bits) and the fields as specified in
  // RFC 4122. Technically we only need the raw data, but might as well unionize here.
  union {
    uint8_t data[16];

    struct {
      uint32_t timeLow;
      uint16_t timeMid;
      uint16_t timeHighAndVersion;
      uint8_t clockSeqAndReserved;
      uint8_t clockSeqLow;
      uint8_t node[6];
    };
  } _uuid;

  // This is the typically used visible portion of the UUID
  TSUuidVersion _version;
  char _string[TS_UUID_STRING_LEN + 1];

  bool
  _toString(char *buf)
  {
    int len = snprintf(buf, TS_UUID_STRING_LEN + 1, "%08x-%04hx-%04hx-%02hhx%02hhx-%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx",
                       _uuid.timeLow, _uuid.timeMid, _uuid.timeHighAndVersion, _uuid.clockSeqAndReserved, _uuid.clockSeqLow,
                       _uuid.node[0], _uuid.node[1], _uuid.node[2], _uuid.node[3], _uuid.node[4], _uuid.node[5]);

    return (len == TS_UUID_STRING_LEN);
  }
};
