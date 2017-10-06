#if !defined I_MIOBUFFERWRITER_H_
#define I_MIOBUFFERWRITER_H_

/** @file

    Buffer Writer for an MIOBuffer.

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

#include <cstring>

#include <ts/ink_assert.h>
#include <ts/BufferWriter.h>

#if !defined(UNIT_TEST_BUFFER_WRITER)
#include <I_IOBuffer.h>
#endif

class MIOBufferWriter : public ts::BufferWriter
{
public:
  MIOBufferWriter(MIOBuffer &miob) : _miob(miob) {}

  MIOBufferWriter &
  write(const void *data_, size_t length) override
  {
    const char *data = static_cast<const char *>(data_);

    while (length) {
      IOBufferBlock *iobbPtr = _miob.first_write_block();

      if (!iobbPtr) {
        addBlock();

        iobbPtr = _miob.first_write_block();

        ink_assert(iobbPtr);
      }

      size_t writeSize = iobbPtr->write_avail();

      if (length < writeSize) {
        writeSize = length;
      }

      std::memcpy(iobbPtr->end(), data, writeSize);
      iobbPtr->fill(writeSize);

      data += writeSize;
      length -= writeSize;

      _numWritten += writeSize;
    }

    return *this;
  }

  MIOBufferWriter &
  write(char c) override
  {
    return write(&c, 1);
  }

  bool
  error() const override
  {
    return false;
  }

  char *
  auxBuffer() override
  {
    IOBufferBlock *iobbPtr = _miob.first_write_block();

    if (!iobbPtr) {
      return nullptr;
    }

    return iobbPtr->end();
  }

  size_t
  auxBufferCapacity() const
  {
    IOBufferBlock *iobbPtr = _miob.first_write_block();

    if (!iobbPtr) {
      return 0;
    }

    return iobbPtr->write_avail();
  }

  // Write the first n characters that have been placed in the auxiliary buffer.  This call invalidates the auxiliary buffer.
  // This function should not be called if no auxiliary buffer is available.
  //
  MIOBufferWriter &
  write(size_t n) override
  {
    if (n) {
      IOBufferBlock *iobbPtr = _miob.first_write_block();

      ink_assert(iobbPtr and (n <= size_t(iobbPtr->write_avail())));

      iobbPtr->fill(n);

      _numWritten += n;
    }

    return *this;
  }

  // No fixed limit on capacity.
  //
  size_t
  capacity() const override
  {
    return (~size_t(0));
  }

  size_t
  extent() const override
  {
    return _numWritten;
  }

  // Not useful in this derived class.
  //
  BufferWriter &clip(size_t) override { return *this; }

  // Not useful in this derived class.
  //
  BufferWriter &extend(size_t) override { return *this; }

  // This must not be called for this derived class.
  //
  const char *
  data() const override
  {
    ink_assert(false);
    return nullptr;
  }

protected:
  MIOBuffer &_miob;

private:
  size_t _numWritten = 0;

  virtual void
  addBlock()
  {
    _miob.add_block();
  }
};

#endif // include once
