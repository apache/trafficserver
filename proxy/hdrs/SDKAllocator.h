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

   SDKAllocator.h

   Description:

 ****************************************************************************/

#ifndef _SDK_ALLOCATOR_H_
#define _SDK_ALLOCATOR_H_

#include "libts.h"
#include "List.h"

/////////////////////////////////////
//   SDK Return Value Allocation   //
/////////////////////////////////////
enum
{
  SDK_ALLOC_MAGIC_DEAD = 0xDEADFFEE,
  SDK_ALLOC_MAGIC_MIME_FIELD_HANDLE = 0xFFEEABCB,
};

class SDKAllocator;
struct MIMEField;
struct MIMEFieldSDKHandle;

struct SDKAllocHdr
{
#ifdef DEBUG
  uint32 m_magic;
  SDKAllocator *m_source;
#endif
  LINK(SDKAllocHdr, link);
};

class SDKAllocator:public DLL<SDKAllocHdr>
{
public:
  SDKAllocator();
  void init();
  MIMEFieldSDKHandle *allocate_mhandle();

  // Object free functions return 1 if
  //   the object has a valid magic number and is
  //   acutually free'd and zero otherize
  int free_mhandle(MIMEFieldSDKHandle * h);

  // Free all the objects on the list
  inkcoreapi void free_all();
};

inline
SDKAllocator::SDKAllocator():
DLL<SDKAllocHdr> ()
{
}

inline void
SDKAllocator::init()
{
  head = NULL;
}

#endif
