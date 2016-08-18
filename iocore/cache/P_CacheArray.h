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

#ifndef __CACHE_ARRAY_H__
#define __CACHE_ARRAY_H__

#define FAST_DATA_SIZE 4

template <class T> struct CacheArray {
  CacheArray(const T *val, int initial_size = 0);
  ~CacheArray();

  operator const T *() const;
  operator T *();
  T &operator[](int idx);
  T &operator()(int idx);
  T *detach();
  int length();
  void clear();
  void
  set_length(int i)
  {
    pos = i - 1;
  }

  void resize(int new_size);

  T *data;
  T fast_data[FAST_DATA_SIZE];
  const T *default_val;
  int size;
  int pos;
};

template <class T>
TS_INLINE
CacheArray<T>::CacheArray(const T *val, int initial_size)
  : data(NULL), default_val(val), size(0), pos(-1)
{
  if (initial_size > 0) {
    int i = 1;

    while (i < initial_size)
      i <<= 1;

    resize(i);
  }
}

template <class T> TS_INLINE CacheArray<T>::~CacheArray()
{
  if (data) {
    if (data != fast_data) {
      delete[] data;
    }
  }
}

template <class T> TS_INLINE CacheArray<T>::operator const T *() const
{
  return data;
}

template <class T> TS_INLINE CacheArray<T>::operator T *()
{
  return data;
}

template <class T> TS_INLINE T &CacheArray<T>::operator[](int idx)
{
  return data[idx];
}

template <class T>
TS_INLINE T &
CacheArray<T>::operator()(int idx)
{
  if (idx >= size) {
    int new_size;

    if (size == 0) {
      new_size = FAST_DATA_SIZE;
    } else {
      new_size = size * 2;
    }

    if (idx >= new_size) {
      new_size = idx + 1;
    }

    resize(new_size);
  }

  if (idx > pos) {
    pos = idx;
  }

  return data[idx];
}

template <class T>
TS_INLINE T *
CacheArray<T>::detach()
{
  T *d;

  d    = data;
  data = NULL;

  return d;
}

template <class T>
TS_INLINE int
CacheArray<T>::length()
{
  return pos + 1;
}

template <class T>
TS_INLINE void
CacheArray<T>::clear()
{
  if (data) {
    if (data != fast_data) {
      delete[] data;
    }
    data = NULL;
  }

  size = 0;
  pos  = -1;
}

template <class T>
TS_INLINE void
CacheArray<T>::resize(int new_size)
{
  if (new_size > size) {
    T *new_data;
    int i;

    if (new_size > FAST_DATA_SIZE) {
      new_data = new T[new_size];
    } else {
      new_data = fast_data;
    }

    for (i = 0; i < size; i++) {
      new_data[i] = data[i];
    }

    for (; i < new_size; i++) {
      new_data[i] = *default_val;
    }

    if (data) {
      if (data != fast_data) {
        delete[] data;
      }
    }
    data = new_data;
    size = new_size;
  }
}

#endif /* __CACHE_ARRAY_H__ */
