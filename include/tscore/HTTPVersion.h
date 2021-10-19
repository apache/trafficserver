/** @file

  HTTPVersion - class to track the HTTP version

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

#pragma once

class HTTPVersion
{
public:
  HTTPVersion() {}
  HTTPVersion(HTTPVersion const &that) = default;
  HTTPVersion &operator=(const HTTPVersion &) = default;

  explicit HTTPVersion(int version);
  constexpr HTTPVersion(uint8_t ver_major, uint8_t ver_minor);

  int operator==(const HTTPVersion &hv) const;
  int operator!=(const HTTPVersion &hv) const;
  int operator>(const HTTPVersion &hv) const;
  int operator<(const HTTPVersion &hv) const;
  int operator>=(const HTTPVersion &hv) const;
  int operator<=(const HTTPVersion &hv) const;

  uint8_t get_major() const;
  uint8_t get_minor() const;
  int get_flat_version() const;

private:
  uint8_t vmajor = 0;
  uint8_t vminor = 0;
};

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline HTTPVersion::HTTPVersion(int version)
{
  vmajor = (version >> 16) & 0xFFFF;
  vminor = version & 0xFFFF;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline constexpr HTTPVersion::HTTPVersion(uint8_t ver_major, uint8_t ver_minor) : vmajor(ver_major), vminor(ver_minor) {}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline uint8_t
HTTPVersion::get_major() const
{
  return vmajor;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline uint8_t
HTTPVersion::get_minor() const
{
  return vminor;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline int
HTTPVersion::get_flat_version() const
{
  return vmajor << 16 | vminor;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline int
HTTPVersion::operator==(const HTTPVersion &hv) const
{
  return vmajor == hv.get_major() && vminor == hv.get_minor();
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline int
HTTPVersion::operator!=(const HTTPVersion &hv) const
{
  return !(*this == hv);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline int
HTTPVersion::operator>(const HTTPVersion &hv) const
{
  return vmajor > hv.get_major() || (vmajor == hv.get_major() && vminor > hv.get_minor());
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline int
HTTPVersion::operator<(const HTTPVersion &hv) const
{
  return vmajor < hv.get_major() || (vmajor == hv.get_major() && vminor < hv.get_minor());
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline int
HTTPVersion::operator>=(const HTTPVersion &hv) const
{
  return vmajor > hv.get_major() || (vmajor == hv.get_major() && vminor >= hv.get_minor());
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline int
HTTPVersion::operator<=(const HTTPVersion &hv) const
{
  return vmajor < hv.get_major() || (vmajor == hv.get_major() && vminor <= hv.get_minor());
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

constexpr HTTPVersion HTTP_INVALID{0, 0};
constexpr HTTPVersion HTTP_0_9{0, 9};
constexpr HTTPVersion HTTP_1_0{1, 0};
constexpr HTTPVersion HTTP_1_1{1, 1};
constexpr HTTPVersion HTTP_2_0{2, 0};
