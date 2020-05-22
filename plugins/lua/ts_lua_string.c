/*
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

#include "ts_lua_string.h"
#include "ts_lua_util.h"

u_char *
ts_lua_hex_dump(u_char *dst, u_char *src, size_t len)
{
  static u_char hex[] = "0123456789abcdef";

  while (len--) {
    *dst++ = hex[*src >> 4];
    *dst++ = hex[*src++ & 0xf];
  }

  return dst;
}

unsigned char
hex_to_int(unsigned char c)
{
  if (c >= '0' && c <= '9') {
    return (c - '0');
  }
  if (c >= 'A' && c <= 'F') {
    return (c - 'A' + 10);
  }
  if (c >= 'a' && c <= 'f') {
    return (c - 'a' + 10);
  }
  return 255;
}

u_char *
ts_lua_hex_to_bin(u_char *dst, u_char *src, size_t len)
{
  if (len % 2 != 0) {
    TSDebug(TS_LUA_DEBUG_TAG, "ts_lua_hex_to_bin(): not an even number of hex digits");
    return NULL;
  }

  for (unsigned int x = 0; x < len; x += 2) {
    unsigned char a = hex_to_int(src[x]);
    unsigned char b = hex_to_int(src[x + 1]);
    if (a == 255 || b == 255) {
      TSDebug(TS_LUA_DEBUG_TAG, "ts_lua_hex_to_bin(): failure in hex to binary conversion");
      return NULL;
    }
    unsigned char result = (a << 4) + b;
    dst[x / 2]           = result;
  }

  return dst;
}
