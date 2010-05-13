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



#include "P_BC_OpenSegment.h"

#include "P_BlockCacheKey.h"

BC_OpenSegment::BC_OpenSegment(ProxyMutex * p)
:Continuation(p)
{
}

BC_OpenSegment::~BC_OpenSegment()
{
}

/**
   internal implementation hidden from rest of code.

   BC_OpenSegment is the interface to the world.  The reason we
   insulate BC_OpenSegment even though it is only used by the cache
   internal code is that we want to be able to cleanly replace the
   current filesystem based implementation with something else such as
   a custom object store and do not want to have to restructure the
   code (which could result in cut and paste errors).

*/
class BCOS_impl:public BC_OpenSegment
{
public:
  BCOS_impl(ProxyMutex * p);
  virtual ~ BCOS_impl();
  int handleCallback(int event, void *data);
  virtual void init(BC_OpenDir * parent, BlockCacheKey * key, BlockCacheDir * dir);
  virtual const BlockCacheKey *getKey();
  virtual Action *verifyKey(Continuation * c);
  virtual Action *close(Continuation * c);
  virtual Action *remove(Continuation * c);
  virtual Action *sync(Continuation * c);
  virtual void registerWriter(BlockCacheSegmentVConnection * vc);
  virtual void registerReader(BlockCacheSegmentVConnection * vc);

private:
  int m_fd;
  BlockCacheKey *m_key;         // local owned copy of key
  BC_OpenDir *m_parent;         // reference to parent dir entry
    Queue<BlockCacheSegmentVConnection> m_readers;   // who is reading
  /**
     who is writing.  XXX: This implicitly assumes only one writer.  I
     think this assumption is reasonable.
  */
  BlockCacheSegmentVConnection *m_writer;
};


BCOS_impl::BCOS_impl(ProxyMutex * p)
:BC_OpenSegment(p)
  , m_fd(-1)
{
  SET_HANDLER(&BCOS_impl::handleCallback);
}

BCOS_impl::~BCOS_impl()
{
}

int
BCOS_impl::init(BC_OpenDir * parent, BlockCacheKey * key, BlockCacheDir * dir)
{
  m_key = key->copy();
  m_parent = parent;
  // XXX: get copy of dir.
}

int
BCOS_impl::handleCallback(int event, void *data)
{
  // handle AIO callbacks
  return EVENT_CONT;
}

void
BCOS_impl::registerWriter(BlockCacheSegmentVConnection * vc)
{
  m_writer = vc;
}

void
BCOS_impl::registerReader(BlockCacheSegmentVConnection * vc)
{
  m_readers.enqueue(vc, vc->opensegment_link);
}


/**
   interface used by cache internals
*/
BC_OpenSegment *
BC_OpenSegment_util::create(ProxyMutex * p)
{
  BCOS_impl *ret = NEW(new BCOS_impl(p));
  return ret;
}

