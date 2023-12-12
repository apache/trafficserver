/** @file

   LocalBuffer

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

#pragma once

namespace ts
{
template <class T = uint8_t, std::size_t EstSizeBound = 1024> class LocalBuffer
{
public:
  LocalBuffer(std::size_t size)
    : _ptr(size == 0 ? nullptr : (size > EstSizeBound ? new T[size] : _buf)),
      _size((0 < size && size <= EstSizeBound) ? EstSizeBound : size)
  {
  }
  ~LocalBuffer();

  // Don't allocate on heap
  void *operator new(std::size_t)   = delete;
  void *operator new[](std::size_t) = delete;

  T *data() const;
  std::size_t size() const;

private:
  T _buf[EstSizeBound];
  T *const _ptr;
  const std::size_t _size;
};

template <class T, std::size_t S> LocalBuffer<T, S>::~LocalBuffer()
{
  if (_ptr && _ptr != _buf) {
    delete[] _ptr;
  }
}

template <class T, std::size_t S>
inline T *
LocalBuffer<T, S>::data() const
{
  return _ptr;
}

template <class T, std::size_t S>
inline std::size_t
LocalBuffer<T, S>::size() const
{
  return _size;
}

} // namespace ts
