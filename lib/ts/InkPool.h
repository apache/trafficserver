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

#ifndef __INK_POOL_H_INCLUDED__
#define __INK_POOL_H_INCLUDED__

//
// Template of a static size pool of objects.
//
//
template <class C> class InkStaticPool
{
public:
  InkStaticPool(int size) : sz1(size + 1), head(0), tail(0) { pool = new C *[sz1]; }

  virtual ~InkStaticPool()
  {
    cleanUp();
    delete[] pool;
  }

  C *get();
  bool put(C *newObj);
  void
  put_or_delete(C *newObj)
  {
    if (!put(newObj))
      delete newObj;
  }

protected:
  void cleanUp(void);

private:
  const int sz1;
  int head;
  int tail;
  C **pool;
};

template <class C>
inline C *
InkStaticPool<C>::get()
{
  if (head != tail) {
    C *res = pool[head++];
    head %= sz1;
    return (res);
  }
  return (NULL);
}

template <class C>
inline bool
InkStaticPool<C>::put(C *newObj)
{
  if (newObj == NULL)
    return (false); // cannot put NULL pointer

  int newTail = (tail + 1) % sz1;
  bool res = (newTail != head);
  if (res) {
    pool[tail] = newObj;
    tail = newTail;
  }
  return (res);
}

template <class C>
inline void
InkStaticPool<C>::cleanUp(void)
{
  while (true) {
    C *tp = get();
    if (tp == NULL)
      break;
    delete tp;
  }
}

#endif // #ifndef __INK_POOL_H_INCLUDED__
