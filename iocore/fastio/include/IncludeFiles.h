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


/*
 * Contains the most common include files.
 *
 *
 */

#ifndef _INCLUDE_FILES_H
#define _INCLUDE_FILES_H

#include <sys/types.h>

#if defined(linux)
#include <stdint.h>
#endif

// XXX: We don't have sunos in configure
#if defined(sunos)
#include <sys/inttypes.h>
#endif

#include <sys/socket.h>

#ifndef _KERNEL
#if defined(sunos)
#include <thread.h>
#include <synch.h>
#endif
#endif

#include <stropts.h>
#if defined(sunos)
#include <sys/stream.h>
#endif

#endif
