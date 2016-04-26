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

//////////////////////////////////////////////////////////////////////////////////////////////
//
// Implement the classes for the various types of hash keys we support.
//
#ifndef __LULU_H__
#define __LULU_H__ 1

#include <string>

#include "ts/ink_defs.h"
#include "ts/ink_platform.h"

std::string getIP(sockaddr const *s_sockaddr);
char *getIP(sockaddr const *s_sockaddr, char res[INET6_ADDRSTRLEN]);
uint16_t getPort(sockaddr const *s_sockaddr);

// Memory barriers
#if defined(__i386__)
#define mb() __asm__ __volatile__("lock; addl $0,0(%%esp)" : : : "memory")
#define rmb() __asm__ __volatile__("lock; addl $0,0(%%esp)" : : : "memory")
#define wmb() __asm__ __volatile__("" : : : "memory")
#elif defined(__x86_64__)
#define mb() __asm__ __volatile__("mfence" : : : "memory")
#define rmb() __asm__ __volatile__("lfence" : : : "memory")
#define wmb() __asm__ __volatile__("" : : : "memory")
#elif defined(__mips__)
#define mb() __asm__ __volatile__("sync" : : : "memory")
#define rmb() __asm__ __volatile__("sync" : : : "memory")
#define wmb() __asm__ __volatile__("" : : : "memory")
#elif defined(__arm__)
#define mb() __asm__ __volatile__("dmb" : : : "memory")
#define rmb() __asm__ __volatile__("dmb" : : : "memory")
#define wmb() __asm__ __volatile__("" : : : "memory")
#elif defined(__mips__)
#define mb() __asm__ __volatile__("sync" : : : "memory")
#define rmb() __asm__ __volatile__("sync" : : : "memory")
#define wmb() __asm__ __volatile__("" : : : "memory")
#elif defined(__powerpc64__)
#define mb() __asm__ __volatile__("sync" : : : "memory")
#define rmb() __asm__ __volatile__("sync" : : : "memory")
#define wmb() __asm__ __volatile__("sync" : : : "memory")
#elif defined(__aarch64__)
#define mb() __asm__ __volatile__("dsb sy" : : : "memory")
#define rmb() __asm__ __volatile__("dsb ld" : : : "memory")
#define wmb() __asm__ __volatile__("dsb st" : : : "memory")
#else
#error "Define barriers"
#endif

extern const char PLUGIN_NAME[];
extern const char PLUGIN_NAME_DBG[];

// From google styleguide: http://google-styleguide.googlecode.com/svn/trunk/cppguide.xml
#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
  TypeName(const TypeName &);              \
  void operator=(const TypeName &)

#endif // __LULU_H__
