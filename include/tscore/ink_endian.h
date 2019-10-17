/** @file
 *
 *  Endian conversion routines
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

#ifdef HAVE_SYS_ENDIAN_H
#include <sys/endian.h>
#endif
#ifdef HAVE_MACHINE_ENDIAN_H
#include <machine/endian.h>
#endif
#ifdef HAVE_ENDIAN_H
#include <endian.h>
#endif
#ifdef HAVE_SYS_BYTEORDER_H
#include <sys/byteorder.h>
#endif

#if defined(darwin)
#include <libkern/OSByteOrder.h>
inline uint64_t
be64toh(uint64_t x)
{
  return OSSwapBigToHostInt64(x);
}
inline uint64_t
htobe64(uint64_t x)
{
  return OSSwapHostToBigInt64(x);
}
inline uint32_t
be32toh(uint32_t x)
{
  return OSSwapBigToHostInt32(x);
}
inline uint32_t
htobe32(uint32_t x)
{
  return OSSwapHostToBigInt32(x);
}
#endif
