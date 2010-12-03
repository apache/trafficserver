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

  SDKAllocator.cc

  Description:


 ****************************************************************************/

#include "SDKAllocator.h"
#include "MIME.h"

static const int SDKAllocHdrSize = ROUND(sizeof(SDKAllocHdr), sizeof(char *));

////////////////////////////////////////////////////////////////////
//    SDK Allocator
////////////////////////////////////////////////////////////////////
MIMEField *
SDKAllocator::allocate_mfield()
{
  int size = SDKAllocHdrSize + sizeof(MIMEField);
  SDKAllocHdr *r = (SDKAllocHdr *) xmalloc(size);

  r->m_magic = SDK_ALLOC_MAGIC_STAND_ALONE_FIELD;
  r->m_source = this;
  r->link.prev = NULL;
  r->link.next = NULL;

  // Put the object on the list so that we can
  //  deallocate it later
  this->push(r);

  MIMEField *f = (MIMEField *) (((char *) r) + SDKAllocHdrSize);

  return f;
}

MIMEFieldSDKHandle *
SDKAllocator::allocate_mhandle()
{
  int size = SDKAllocHdrSize + sizeof(MIMEFieldSDKHandle);
  SDKAllocHdr *r = (SDKAllocHdr *) xmalloc(size);

  r->m_magic = SDK_ALLOC_MAGIC_MIME_FIELD_HANDLE;
  r->m_source = this;
  r->link.prev = NULL;
  r->link.next = NULL;

  // Put the object on the list so that we can
  //  deallocate it later
  this->push(r);

  MIMEFieldSDKHandle *h = (MIMEFieldSDKHandle *) (((char *) r) + SDKAllocHdrSize);

  return h;
}

int
SDKAllocator::free_mfield(MIMEField * f)
{
  SDKAllocHdr *obj = (SDKAllocHdr *) (((char *) f) - SDKAllocHdrSize);

  // Sanity check the object to make sure it's
  //   good and from the correct allocator
  if (obj->m_magic != SDK_ALLOC_MAGIC_STAND_ALONE_FIELD) {
    return 0;
  }

  if (obj->m_source != this) {
    return 0;
  }

  this->remove(obj);
  xfree(obj);
  return 1;
}

int
SDKAllocator::free_mhandle(MIMEFieldSDKHandle * h)
{
  SDKAllocHdr *obj = (SDKAllocHdr *) (((char *) h) - SDKAllocHdrSize);

  // Sanity check the object to make sure it's
  //   good and from the correct allocator
  if (obj->m_magic != SDK_ALLOC_MAGIC_MIME_FIELD_HANDLE) {
    return 0;
  }

  if (obj->m_source != this) {
    return 0;
  }

  this->remove(obj);
  xfree(obj);
  return 1;
}

void
SDKAllocator::free_all()
{
  SDKAllocHdr *obj;

  while ((obj = this->pop())) {

    if (obj->m_source != this) {
      // Bad element in list
      ink_assert(0);
    }

    switch (obj->m_magic) {
    case SDK_ALLOC_MAGIC_MIME_FIELD_HANDLE:
    case SDK_ALLOC_MAGIC_STAND_ALONE_FIELD:
      xfree(obj);
      break;
    case SDK_ALLOC_MAGIC_DEAD:
    default:
      // Bad element
      ink_assert(0);
    }
  }
}
