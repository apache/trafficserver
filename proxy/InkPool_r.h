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

#ifndef __INK_POOL_R_H_INCLUDED__
#define __INK_POOL_R_H_INCLUDED__

#include "InkPool.h"
#include "P_EventSystem.h"

template <class C> class InkStaticPool_r : public InkStaticPool<C>
{
public:
  InkStaticPool_r(int size) : InkStaticPool<C>(size) { mutex = new_ProxyMutex(); }

  virtual ~InkStaticPool_r()
  {
    MUTEX_LOCK(lock, mutex, this_ethread());
    cleanUp();
  }

  C *
  get()
  {
    MUTEX_LOCK(lock, mutex, this_ethread());
    return (InkStaticPool<C>::get());
  }

  bool
  put(C *newObj)
  {
    MUTEX_LOCK(lock, mutex, this_ethread());
    return (InkStaticPool<C>::put(newObj));
  }

  void
  put_or_delete(C *newObj)
  {
    if (!this->put(newObj))
      delete newObj;
  }

  ProxyMutex *
  getMutex()
  {
    return (mutex);
  }

protected:
  Ptr<ProxyMutex> mutex;
};


#endif // #ifndef __INK_POOL_R_H_INCLUDED__
