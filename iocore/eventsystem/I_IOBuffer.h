/** @file

  I/O classes

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

  @section watermark Watermark

  Watermarks can be used as an interface between the data transferring
  layer (VConnection) and the user layer (a state machine).  Watermarks
  should be used when you need to have at least a certain amount of data
  to make some determination.  For example, when parsing a string, one
  might wish to ensure that an entire line will come in before consuming
  the data.  In such a case, the water_mark should be set to the largest
  possible size of the string. (appropriate error handling should take
  care of exessively long strings).

  In all other cases, especially when all data will be consumed, the
  water_mark should be set to 0 (the default).

 */

#pragma once
#define I_IOBuffer_h

#include "tscore/ink_platform.h"
#include "tscore/ink_apidefs.h"
#include "tscore/Allocator.h"
#include "tscore/Ptr.h"
#include "tscore/ink_assert.h"
#include "tscore/ink_resource.h"

struct MIOBufferAccessor;

class MIOBuffer;
class IOBufferReader;
class VIO;

// Removing this optimization since this is breaking WMT over HTTP
//#define WRITE_AND_TRANSFER

inkcoreapi extern int64_t max_iobuffer_size;
extern int64_t default_small_iobuffer_size;
extern int64_t default_large_iobuffer_size; // matched to size of OS buffers

#if !defined(TRACK_BUFFER_USER)
#define TRACK_BUFFER_USER 1
#endif

enum AllocType {
  NO_ALLOC,
  FAST_ALLOCATED,
  XMALLOCED,
  MEMALIGNED,
  DEFAULT_ALLOC,
  CONSTANT,
};

#define DEFAULT_BUFFER_NUMBER 128
#define DEFAULT_HUGE_BUFFER_NUMBER 32
#define MAX_MIOBUFFER_READERS 5
#define DEFAULT_BUFFER_ALIGNMENT 8192 // should be disk/page size
#define DEFAULT_BUFFER_BASE_SIZE 128

////////////////////////////////////////////////
// These are defines so that code that used 2 //
// for buffer size index when 2 was 2K will   //
// still work if it uses BUFFER_SIZE_INDEX_2K //
// instead.                                   //
////////////////////////////////////////////////
#define BUFFER_SIZE_INDEX_128 0
#define BUFFER_SIZE_INDEX_256 1
#define BUFFER_SIZE_INDEX_512 2
#define BUFFER_SIZE_INDEX_1K 3
#define BUFFER_SIZE_INDEX_2K 4
#define BUFFER_SIZE_INDEX_4K 5
#define BUFFER_SIZE_INDEX_8K 6
#define BUFFER_SIZE_INDEX_16K 7
#define BUFFER_SIZE_INDEX_32K 8
#define BUFFER_SIZE_INDEX_64K 9
#define BUFFER_SIZE_INDEX_128K 10
#define BUFFER_SIZE_INDEX_256K 11
#define BUFFER_SIZE_INDEX_512K 12
#define BUFFER_SIZE_INDEX_1M 13
#define BUFFER_SIZE_INDEX_2M 14
#define MAX_BUFFER_SIZE_INDEX 14
#define DEFAULT_BUFFER_SIZES (MAX_BUFFER_SIZE_INDEX + 1)

#define BUFFER_SIZE_FOR_INDEX(_i) (DEFAULT_BUFFER_BASE_SIZE * (1 << (_i)))
#define DEFAULT_SMALL_BUFFER_SIZE BUFFER_SIZE_INDEX_512
#define DEFAULT_LARGE_BUFFER_SIZE BUFFER_SIZE_INDEX_4K
#define DEFAULT_TS_BUFFER_SIZE BUFFER_SIZE_INDEX_8K
#define DEFAULT_MAX_BUFFER_SIZE BUFFER_SIZE_FOR_INDEX(MAX_BUFFER_SIZE_INDEX)
#define MIN_IOBUFFER_SIZE BUFFER_SIZE_INDEX_128
#define MAX_IOBUFFER_SIZE (DEFAULT_BUFFER_SIZES - 1)

#define BUFFER_SIZE_ALLOCATED(_i) (BUFFER_SIZE_INDEX_IS_FAST_ALLOCATED(_i) || BUFFER_SIZE_INDEX_IS_XMALLOCED(_i))

#define BUFFER_SIZE_NOT_ALLOCATED DEFAULT_BUFFER_SIZES
#define BUFFER_SIZE_INDEX_IS_XMALLOCED(_size_index) (_size_index < 0)
#define BUFFER_SIZE_INDEX_IS_FAST_ALLOCATED(_size_index) (((uint64_t)_size_index) < DEFAULT_BUFFER_SIZES)
#define BUFFER_SIZE_INDEX_IS_CONSTANT(_size_index) (_size_index >= DEFAULT_BUFFER_SIZES)

#define BUFFER_SIZE_FOR_XMALLOC(_size) (-(_size))
#define BUFFER_SIZE_INDEX_FOR_XMALLOC_SIZE(_size) (-(_size))

#define BUFFER_SIZE_FOR_CONSTANT(_size) (_size - DEFAULT_BUFFER_SIZES)
#define BUFFER_SIZE_INDEX_FOR_CONSTANT_SIZE(_size) (_size + DEFAULT_BUFFER_SIZES)

inkcoreapi extern Allocator ioBufAllocator[DEFAULT_BUFFER_SIZES];

void init_buffer_allocators(int iobuffer_advice);

/**
  A reference counted wrapper around fast allocated or malloced memory.
  The IOBufferData class provides two basic services around a portion
  of allocated memory.

  First, it is a reference counted object and ...

  @remarks The AllocType enum, is used to define the type of allocation
  for the memory this IOBufferData object manages.

  <table>
    <tr>
      <td align="center">AllocType</td>
      <td align="center">Meaning</td>
    </tr>
    <tr>
      <td>NO_ALLOC</td>
      <td></td>
    </tr>
    <tr>
      <td>FAST_ALLOCATED</td>
      <td></td>
    </tr>
    <tr>
      <td>XMALLOCED</td>
      <td></td>
    </tr>
    <tr>
      <td>MEMALIGNED</td>
      <td></td>
    </tr>
    <tr>
      <td>DEFAULT_ALLOC</td>
      <td></td>
    </tr>
    <tr>
      <td>CONSTANT</td>
      <td></td>
    </tr>
  </table>

 */
class IOBufferData : public RefCountObj
{
public:
  /**
    The size of the memory allocated by this IOBufferData. Calculates
    the amount of memory allocated by this IOBufferData.

    @return number of bytes allocated for the '_data' member.

  */
  int64_t block_size();

  /**
    Frees the memory managed by this IOBufferData.  Deallocates the
    memory previously allocated by this IOBufferData object. It frees
    the memory pointed to by '_data' according to the '_mem_type' and
    '_size_index' members.

  */
  void dealloc();

  /**
    Allocates memory and sets this IOBufferData to point to it.
    Allocates memory according to the size_index and type
    parameters. Any previously allocated memory pointed to by
    this IOBufferData is deallocated.

    @param size_index
    @param type of allocation to use; see remarks section.
  */
  void alloc(int64_t size_index, AllocType type = DEFAULT_ALLOC);

  /**
    Provides access to the allocated memory. Returns the address of the
    allocated memory handled by this IOBufferData.

    @return address of the memory handled by this IOBufferData.

  */
  char *
  data()
  {
    return _data;
  }

  /**
    Cast operator. Provided as a convenience, the cast to a char* applied
    to the IOBufferData returns the address of the memory handled by the
    IOBuffer data. In this manner, objects of this class can be used as
    parameter to functions requiring a char*.

  */
  operator char *() { return _data; }
  /**
    Frees the IOBufferData object and its underlying memory. Deallocates
    the memory managed by this IOBufferData and then frees itself. You
    should not use this object or reference after this call.

  */
  void free() override;

  int64_t _size_index;

  /**
    Type of allocation used for the managed memory. Stores the type of
    allocation used for the memory currently managed by the IOBufferData
    object. Do not set or modify this value directly. Instead use the
    alloc or dealloc methods.

  */
  AllocType _mem_type;

  /**
    Points to the allocated memory. This member stores the address of
    the allocated memory. You should not modify its value directly,
    instead use the alloc or dealloc methods.

  */
  char *_data;

#ifdef TRACK_BUFFER_USER
  const char *_location;
#endif

  /**
    Constructor. Initializes state for a IOBufferData object. Do not use
    this method. Use one of the functions with the 'new_' prefix instead.

  */
  IOBufferData()
    : _size_index(BUFFER_SIZE_NOT_ALLOCATED),
      _mem_type(NO_ALLOC),
      _data(nullptr)
#ifdef TRACK_BUFFER_USER
      ,
      _location(nullptr)
#endif
  {
  }

  // noncopyable, declaration only
  IOBufferData(const IOBufferData &) = delete;
  IOBufferData &operator=(const IOBufferData &) = delete;
};

inkcoreapi extern ClassAllocator<IOBufferData> ioDataAllocator;

/**
  A linkable portion of IOBufferData. IOBufferBlock is a chainable
  buffer block descriptor. The IOBufferBlock represents both the used
  and available space in the underlying block. The IOBufferBlock is not
  sharable between buffers but rather represents what part of the data
  block is both in use and usable by the MIOBuffer it is attached to.

*/
class IOBufferBlock : public RefCountObj
{
public:
  /**
    Access the actual data. Provides access to rhe underlying data
    managed by the IOBufferData.

    @return pointer to the underlying data.

  */
  char *
  buf()
  {
    return data->_data;
  }

  /**
    Beginning of the inuse section. Returns the position in the buffer
    where the inuse area begins.

    @return pointer to the start of the inuse section.

  */
  char *
  start()
  {
    return _start;
  }

  /**
    End of the used space. Returns a pointer to end of the used space
    in the data buffer represented by this block.

    @return pointer to the end of the inuse portion of the block.

  */
  char *
  end()
  {
    return _end;
  }

  /**
    End of the data buffer. Returns a pointer to end of the data buffer
    represented by this block.

  */
  char *
  buf_end()
  {
    return _buf_end;
  }

  /**
    Size of the inuse area. Returns the size of the current inuse area.

    @return bytes occupied by the inuse area.

  */
  int64_t
  size()
  {
    return (int64_t)(_end - _start);
  }

  /**
    Size of the data available for reading. Returns the size of the data
    available for reading in the inuse area.

    @return bytes available for reading from the inuse area.

  */
  int64_t
  read_avail() const
  {
    return (int64_t)(_end - _start);
  }

  /**
    Space available in the buffer. Returns the number of bytes that can
    be written to the data buffer.

    @return space available for writing in this IOBufferBlock.
  */
  int64_t
  write_avail()
  {
    return (int64_t)(_buf_end - _end);
  }

  /**
    Size of the memory allocated by the underlying IOBufferData.
    Computes the size of the entire block, which includes the used and
    available areas. It is the memory allocated by the IOBufferData
    referenced by this IOBufferBlock.

    @return bytes allocated to the IOBufferData referenced by this
      IOBufferBlock.

  */
  int64_t
  block_size()
  {
    return data->block_size();
  }

  /**
    Decrease the size of the inuse area. Moves forward the start of
    the inuse area. This also decreases the number of available bytes
    for reading.

    @param len bytes to consume or positions to skip for the start of
      the inuse area.

  */
  void consume(int64_t len);

  /**
    Increase the inuse area of the block. Adds 'len' bytes to the inuse
    area of the block. Data should be copied into the data buffer by
    using end() to find the start of the free space in the data buffer
    before calling fill()

    @param len bytes to increase the inuse area. It must be less than
      or equal to the value of write_avail().

  */
  void fill(int64_t len);

  /**
    Reset the inuse area. The start and end of the inuse area are reset
    but the actual IOBufferData referenced by this IOBufferBlock is not
    modified.  This effectively reduces the number of bytes available
    for reading to zero, and the number of bytes available for writing
    to the size of the entire buffer.

  */
  void reset();

  /**
    Create a copy of the IOBufferBlock. Creates and returns a copy of this
    IOBufferBlock that references the same data that this IOBufferBlock
    (it does not allocate an another buffer). The cloned block will not
    have a writable space since the original IOBufferBlock mantains the
    ownership for writing data to the block.

    @return copy of this IOBufferBlock.

  */
  IOBufferBlock *clone() const;

  /**
    Clear the IOBufferData this IOBufferBlock handles. Clears this
    IOBufferBlock's reference to the data buffer (IOBufferData). You can
    use alloc after this call to allocate an IOBufferData associated to
    this IOBufferBlock.

  */
  void clear();

  /**
    Allocate a data buffer. Allocates a data buffer for this IOBufferBlock
    based on index 'i'.  Index values are described in the remarks
    section in MIOBuffer.

  */
  void alloc(int64_t i = default_large_iobuffer_size);

  /**
    Clear the IOBufferData this IOBufferBlock handles. Clears this
    IOBufferBlock's reference to the data buffer (IOBufferData).

  */
  void dealloc();

  /**
    Set or replace this IOBufferBlock's IOBufferData member. Sets this
    IOBufferBlock's IOBufferData member to point to the IOBufferData
    passed in. You can optionally specify the inuse area with the 'len'
    argument and an offset for the start.

    @param d new IOBufferData this IOBufferBlock references.
    @param len in use area to set. It must be less than or equal to the
      length of the block size *IOBufferData).
    @param offset bytes to skip from the beginning of the IOBufferData
      and to mark its start.

  */
  void set(IOBufferData *d, int64_t len = 0, int64_t offset = 0);
  void set_internal(void *b, int64_t len, int64_t asize_index);
  void realloc_set_internal(void *b, int64_t buf_size, int64_t asize_index);
  void realloc(void *b, int64_t buf_size);
  void realloc(int64_t i);
  void realloc_xmalloc(void *b, int64_t buf_size);
  void realloc_xmalloc(int64_t buf_size);

  /**
    Frees the IOBufferBlock object and its underlying memory.
    Removes the reference to the IOBufferData object and then frees
    itself. You should not use this object or reference after this
    call.

  */
  void free() override;

  char *_start;
  char *_end;
  char *_buf_end;

#ifdef TRACK_BUFFER_USER
  const char *_location;
#endif

  /**
    The underlying reference to the allocated memory. A reference to a
    IOBufferData representing the memory allocated to this buffer. Do
    not set or modify its value directly.

  */
  Ptr<IOBufferData> data;

  /**
    Reference to another IOBufferBlock. A reference to another
    IOBufferBlock that allows this object to link to other.

  */
  Ptr<IOBufferBlock> next;

  /**
    Constructor of a IOBufferBlock. Do not use it to create a new object,
    instead call new_IOBufferBlock

  */
  IOBufferBlock();

  // noncopyable
  IOBufferBlock(const IOBufferBlock &) = delete;
  IOBufferBlock &operator=(const IOBufferBlock &) = delete;
};

extern inkcoreapi ClassAllocator<IOBufferBlock> ioBlockAllocator;

/** A class for holding a chain of IO buffer blocks.
    This class is intended to be used as a member variable for other classes that
    need to anchor an IO Buffer chain but don't need the full @c MIOBuffer machinery.
    That is, the owner is the only reader/writer of the data.

    This does not handle incremental reads or writes well. The intent is that data is
    placed in the instance, held for a while, then used and discarded.

    @note Contrast also with @c IOBufferReader which is similar but requires an
    @c MIOBuffer as its owner.
*/
class IOBufferChain
{
  using self_type = IOBufferChain; ///< Self reference type.

public:
  /// Default constructor - construct empty chain.
  IOBufferChain() = default;
  /// Shallow copy.
  self_type &operator=(self_type const &that);

  /// Shallow append.
  self_type &operator+=(self_type const &that);

  /// Number of bytes of content.
  int64_t length() const;

  /// Copy a chain of @a blocks in to this object up to @a length bytes.
  /// If @a offset is greater than 0 that many bytes are skipped. Those bytes do not count
  /// as part of @a length.
  /// This creates a new chain using existing data blocks. This
  /// breaks the original chain so that changes there (such as appending blocks)
  /// is not reflected in this chain.
  /// @return The number of bytes written to the chain.
  int64_t write(IOBufferBlock *blocks, int64_t length, int64_t offset = 0);

  /// Add the content of a buffer block.
  /// The buffer block is unchanged.
  int64_t write(IOBufferData *data, int64_t length = 0, int64_t offset = 0);

  /// Remove @a size bytes of content from the front of the chain.
  /// @return The actual number of bytes removed.
  int64_t consume(int64_t size);

  /// Clear current chain.
  void clear();

  /// Get the first block.
  IOBufferBlock *head();
  IOBufferBlock const *head() const;

  /// STL Container support.

  /// Block iterator.
  /// @internal The reason for this is to override the increment operator.
  class const_iterator : public std::forward_iterator_tag
  {
    using self_type = const_iterator; ///< Self reference type.
  protected:
    /// Current buffer block.
    IOBufferBlock *_b = nullptr;

  public:
    using value_type = const IOBufferBlock; ///< Iterator value type.

    const_iterator() = default; ///< Default constructor.

    /// Copy constructor.
    const_iterator(self_type const &that);

    /// Assignment.
    self_type &operator=(self_type const &that);

    /// Equality.
    bool operator==(self_type const &that) const;
    /// Inequality.
    bool operator!=(self_type const &that) const;

    value_type &operator*() const;
    value_type *operator->() const;

    self_type &operator++();
    self_type operator++(int);
  };

  class iterator : public const_iterator
  {
    using self_type = iterator; ///< Self reference type.
  public:
    using value_type = IOBufferBlock; ///< Dereferenced type.

    value_type &operator*() const;
    value_type *operator->() const;
  };

  using value_type = IOBufferBlock;

  iterator begin();
  const_iterator begin() const;

  iterator end();
  const_iterator end() const;

protected:
  /// Append @a block.
  void append(IOBufferBlock *block);

  /// Head of buffer block chain.
  Ptr<IOBufferBlock> _head;
  /// Tail of the block chain.
  IOBufferBlock *_tail = nullptr;
  /// The amount of data of interest.
  /// Not necessarily the amount of data in the chain of blocks.
  int64_t _len = 0;
};

/**
  An independent reader from an MIOBuffer. A reader for a set of
  IOBufferBlocks. The IOBufferReader represents the place where a given
  consumer of buffer data is reading from. It provides a uniform interface
  for easily accessing the data contained in a list of IOBufferBlocks
  associated with the IOBufferReader.

  IOBufferReaders are the abstraction that determine when data blocks
  can be removed from the buffer.

*/
class IOBufferReader
{
public:
  /**
    Start of unconsumed data. Returns a pointer to first unconsumed data
    on the buffer for this reader. A null pointer indicates no data is
    available. It uses the current start_offset value.

    @return pointer to the start of the unconsumed data.

  */
  char *start();

  /**
    End of inuse area of the first block with unconsumed data. Returns a
    pointer to the end of the first block with unconsumed data for this
    reader. A nullptr pointer indicates there are no blocks with unconsumed
    data for this reader.

    @return pointer to the end of the first block with unconsumed data.

  */
  char *end();

  /**
    Amount of data available across all of the IOBufferBlocks. Returns the
    number of unconsumed bytes of data available to this reader across
    all remaining IOBufferBlocks. It subtracts the current start_offset
    value from the total.

    @return bytes of data available across all the buffers.

  */
  int64_t read_avail();

  /** Check if there is more than @a size bytes available to read.
      @return @c true if more than @a size byte are available.
  */
  bool is_read_avail_more_than(int64_t size);

  /**
    Number of IOBufferBlocks with data in the block list. Returns the
    number of IOBufferBlocks on the block list with data remaining for
    this reader.

    @return number of blocks with data for this reader.

  */
  int block_count();

  /**
    Amount of data available in the first buffer with data for this
    reader.  Returns the number of unconsumed bytes of data available
    on the first IOBufferBlock with data for this reader.

    @return number of unconsumed bytes of data available in the first
      buffer.

  */
  int64_t block_read_avail();

  void skip_empty_blocks();

  /**
    Clears all fields in this IOBuffeReader, rendering it unusable. Drops
    the reference to the IOBufferBlock list, the accesor, MIOBuffer and
    resets this reader's state. You have to set those fields in order
    to use this object again.

  */
  void clear();

  /**
    Instruct the reader to reset the IOBufferBlock list. Resets the
    reader to the point to the start of the block where new data will
    be written. After this call, the start_offset field is set to zero
    and the list of IOBufferBlocks is set using the associated MIOBuffer.

  */
  void reset();

  /**
    Consume a number of bytes from this reader's IOBufferBlock
    list. Advances the current position in the IOBufferBlock list of
    this reader by n bytes.

    @param n number of bytes to consume. It must be less than or equal
      to read_avail().

  */
  void consume(int64_t n);

  /**
    Create another reader with access to the same data as this
    IOBufferReader. Allocates a new reader with the same state as this
    IOBufferReader. This means that the new reader will point to the same
    list of IOBufferBlocks and to the same buffer position as this reader.

    @return new reader with the same state as this.

  */
  IOBufferReader *clone();

  /**
    Deallocate this reader. Removes and deallocates this reader from
    the underlying MIOBuffer. This IOBufferReader object must not be
    used after this call.

  */
  void dealloc();

  /**
    Get a pointer to the first block with data. Returns a pointer to
    the first IOBufferBlock in the block chain with data available for
    this reader

    @return pointer to the first IOBufferBlock in the list with data
      available for this reader.

  */
  IOBufferBlock *get_current_block();

  /**
    Consult this reader's MIOBuffer writable space. Queries the MIOBuffer
    associated with this reader about the amount of writable space
    available without adding any blocks on the buffer and returns true
    if it is less than the water mark.

    @return true if the MIOBuffer associated with this IOBufferReader
      returns true in MIOBuffer::current_low_water().

  */
  bool current_low_water();

  /**
    Queries the underlying MIOBuffer about. Returns true if the amount
    of writable space after adding a block on the underlying MIOBuffer
    is less than its water mark. This function call may add blocks to
    the MIOBuffer (see MIOBuffer::low_water()).

    @return result of MIOBuffer::low_water() on the MIOBuffer for
      this reader.

  */
  bool low_water();

  /**
    To see if the amount of data available to the reader is greater than
    the MIOBuffer's water mark. Indicates whether the amount of data
    available to this reader exceeds the water mark for this reader's
    MIOBuffer.

    @return true if the amount of data exceeds the MIOBuffer's water mark.

  */
  bool high_water();

  /**
    Perform a memchr() across the list of IOBufferBlocks. Returns the
    offset from the current start point of the reader to the first
    occurrence of character 'c' in the buffer.

    @param c character to look for.
    @param len number of characters to check. If len exceeds the number
      of bytes available on the buffer or INT64_MAX is passed in, the
      number of bytes available to the reader is used. It is independent
      of the offset value.
    @param offset number of the bytes to skip over before beginning
      the operation.
    @return -1 if c is not found, otherwise position of the first
      ocurrence.

  */
  inkcoreapi int64_t memchr(char c, int64_t len = INT64_MAX, int64_t offset = 0);

  /**
    Copies and consumes data. Copies len bytes of data from the buffer
    into the supplied buffer, which must be allocated prior to the call
    and it must be at large enough for the requested bytes. Once the
    data is copied, it consumed from the reader.

    @param buf in which to place the data.
    @param len bytes to copy and consume. If 'len' exceeds the bytes
      available to the reader, the number of bytes available is used
      instead.

    @return number of bytes copied and consumed.

  */
  inkcoreapi int64_t read(void *buf, int64_t len);

  /**
    Copy data but do not consume it. Copies 'len' bytes of data from
    the current buffer into the supplied buffer. The copy skips the
    number of bytes specified by 'offset' beyond the current point of
    the reader. It also takes into account the current start_offset value.

    @param buf in which to place the data. The pointer is modified after
      the call and points one position after the end of the data copied.
    @param len bytes to copy. If len exceeds the bytes available to the
      reader or INT64_MAX is passed in, the number of bytes available is
      used instead. No data is consumed from the reader in this operation.
    @param offset bytes to skip from the current position. The parameter
      is modified after the call.
    @return pointer to one position after the end of the data copied. The
      parameter buf is set to this value also.

  */
  inkcoreapi char *memcpy(const void *buf, int64_t len = INT64_MAX, int64_t offset = 0);

  /**
    Subscript operator. Returns a reference to the character at the
    specified position. You must ensure that it is within an appropriate
    range.

    @param i positions beyond the current point of the reader. It must
      be less than the number of the bytes available to the reader.

    @return reference to the character in that position.

  */
  char &operator[](int64_t i);

  MIOBuffer *
  writer() const
  {
    return mbuf;
  }
  MIOBuffer *
  allocated() const
  {
    return mbuf;
  }

  MIOBufferAccessor *accessor; // pointer back to the accessor

  /**
    Back pointer to this object's MIOBuffer. A pointer back to the
    MIOBuffer this reader is allocated from.

  */
  MIOBuffer *mbuf;
  Ptr<IOBufferBlock> block;

  /**
    Offset beyond the shared start(). The start_offset is used in the
    calls that copy or consume data and is an offset at the beginning
    of the available data.

  */
  int64_t start_offset;
  int64_t size_limit;

  IOBufferReader() : accessor(nullptr), mbuf(nullptr), start_offset(0), size_limit(INT64_MAX) {}
};

/**
  A multiple reader, single writer memory buffer. MIOBuffers are at
  the center of all IOCore data transfer. MIOBuffers are the data
  buffers used to transfer data to and from VConnections. A MIOBuffer
  points to a list of IOBufferBlocks which in turn point to IOBufferData
  structures that in turn point to the actual data. MIOBuffer allows one
  producer and multiple consumers. The buffer fills up according the
  amount of data outstanding for the slowest consumer. Thus, MIOBuffer
  implements automatic flow control between readers of different speeds.
  Data on IOBuffer is immutable. Once written it cannot be modified, only
  deallocated once all consumers have finished with it. Immutability is
  necessary since data can be shared between buffers, which means that
  multiple IOBufferBlock objects may reference the same data but only
  one will have ownership for writing.

*/
class MIOBuffer
{
public:
  /**
    Increase writer's inuse area. Instructs the writer associated with
    this MIOBuffer to increase the inuse area of the block by as much as
    'len' bytes.

    @param len number of bytes to add to the inuse area of the block.

  */
  void fill(int64_t len);

  /**
    Adds a block to the end of the block list. The block added to list
    must be writable by this buffer and must not be writable by any
    other buffer.

  */
  void append_block(IOBufferBlock *b);

  /**
    Adds a new block to the end of the block list. The size is determined
    by asize_index. See the remarks section for a mapping of indexes to
    buffer block sizes.

  */
  void append_block(int64_t asize_index);

  /**
    Adds new block to the end of block list using the block size for
    the buffer specified when the buffer was allocated.

  */
  void add_block();

  /**
    Adds by reference len bytes of data pointed to by b to the end
    of the buffer.  b MUST be a pointer to the beginning of  block
    allocated from the ats_xmalloc() routine. The data will be deallocated
    by the buffer once all readers on the buffer have consumed it.

  */
  void append_xmalloced(void *b, int64_t len);

  /**
    Adds by reference len bytes of data pointed to by b to the end of the
    buffer. b MUST be a pointer to the beginning of  block allocated from
    ioBufAllocator of the corresponding index for fast_size_index. The
    data will be deallocated by the buffer once all readers on the buffer
    have consumed it.

  */
  void append_fast_allocated(void *b, int64_t len, int64_t fast_size_index);

  /**
    Adds the nbytes worth of data pointed by rbuf to the buffer. The
    data is copied into the buffer. write() does not respect watermarks
    or buffer size limits. Users of write must implement their own flow
    control. Returns the number of bytes added.

  */
  inkcoreapi int64_t write(const void *rbuf, int64_t nbytes);

#ifdef WRITE_AND_TRANSFER
  /**
    Same functionality as write but for the one small difference. The
    space available in the last block is taken from the original and
    this space becomes available to the copy.

  */
  inkcoreapi int64_t write_and_transfer_left_over_space(IOBufferReader *r, int64_t len = INT64_MAX, int64_t offset = 0);
#endif

  /**
    Add by data from IOBufferReader r to the this buffer by reference. If
    len is INT64_MAX, all available data on the reader is added. If len is
    less than INT64_MAX, the smaller of len or the amount of data on the
    buffer is added. If offset is greater than zero, than the offset
    bytes of data at the front of the reader are skipped. Bytes skipped
    by offset reduce the number of bytes available on the reader used
    in the amount of data to add computation. write() does not respect
    watermarks or buffer size limits. Users of write must implement
    their own flow control. Returns the number of bytes added. Each
    write() call creates a new IOBufferBlock, even if it is for one
    byte. As such, it's necessary to exercise caution in any code that
    repeatedly transfers data from one buffer to another, especially if
    the data is being read over the network as it may be coming in very
    small chunks. Because deallocation of outstanding buffer blocks is
    recursive, it's possible to overrun the stack if too many blocks
    have been added to the buffer chain. It's imperative that users
    both implement their own flow control to prevent too many bytes
    from becoming outstanding on a buffer that the write() call is
    being used and that care be taken to ensure the transfers are of a
    minimum size. Should it be necessary to make a large number of small
    transfers, it's preferable to use a interface that copies the data
    rather than sharing blocks to prevent a build of blocks on the buffer.

  */
  inkcoreapi int64_t write(IOBufferReader *r, int64_t len = INT64_MAX, int64_t offset = 0);

  /** Copy data from the @a chain to this buffer.
      New IOBufferBlocks are allocated so this gets a copy of the data that is independent of the source.
      @a offset bytes are skipped at the start of the @a chain. The length is bounded by @a len and the
      size in the @a chain.

      @return the number of bytes copied.

      @internal I do not like counting @a offset against @a bytes but that's how @c write works...
  */
  int64_t write(IOBufferChain const *chain, int64_t len = INT64_MAX, int64_t offset = 0);

  int64_t remove_append(IOBufferReader *);

  /**
    Returns a pointer to the first writable block on the block chain.
    Returns nullptr if there are not currently any writable blocks on the
    block list.
  */
  IOBufferBlock *
  first_write_block()
  {
    if (_writer) {
      if (_writer->next && !_writer->write_avail()) {
        return _writer->next.get();
      }
      ink_assert(!_writer->next || !_writer->next->read_avail());
      return _writer.get();
    }

    return nullptr;
  }

  char *
  buf()
  {
    IOBufferBlock *b = first_write_block();
    return b ? b->buf() : nullptr;
  }

  char *
  buf_end()
  {
    return first_write_block()->buf_end();
  }

  char *
  start()
  {
    return first_write_block()->start();
  }

  char *
  end()
  {
    return first_write_block()->end();
  }

  /**
    Returns the amount of space of available for writing on the first
    writable block on the block chain (the one that would be reutrned
    by first_write_block()).

  */
  int64_t block_write_avail();

  /**
    Returns the amount of space of available for writing on all writable
    blocks currently on the block chain.  Will NOT add blocks to the
    block chain.

  */
  int64_t current_write_avail();

  /**
    Adds blocks for writing if the watermark criteria are met. Returns
    the amount of space of available for writing on all writable blocks
    on the block chain after a block due to the watermark criteria.

  */
  int64_t write_avail();

  /**
    Returns the default data block size for this buffer.

  */
  int64_t block_size();

  /**
    Returns the default data block size for this buffer.

  */
  int64_t
  total_size()
  {
    return block_size();
  }

  /**
    Returns true if amount of the data outstanding on the buffer exceeds
    the watermark.

  */
  bool
  high_water()
  {
    return max_read_avail() > water_mark;
  }

  /**
    Returns true if the amount of writable space after adding a block on
    the buffer is less than the water mark. Since this function relies
    on write_avail() it may add blocks.

  */
  bool
  low_water()
  {
    return write_avail() <= water_mark;
  }

  /**
    Returns true if amount the amount writable space without adding and
    blocks on the buffer is less than the water mark.

  */
  bool
  current_low_water()
  {
    return current_write_avail() <= water_mark;
  }
  void set_size_index(int64_t size);

  /**
    Allocates a new IOBuffer reader and sets it's its 'accessor' field
    to point to 'anAccessor'.

  */
  IOBufferReader *alloc_accessor(MIOBufferAccessor *anAccessor);

  /**
    Allocates an IOBufferReader for this buffer. IOBufferReaders hold
    data on the buffer for different consumers. IOBufferReaders are
    REQUIRED when using buffer. alloc_reader() MUST ONLY be a called
    on newly allocated buffers. Calling on a buffer with data already
    placed on it will result in the reader starting at an indeterminate
    place on the buffer.

  */
  IOBufferReader *alloc_reader();

  /**
    Allocates a new reader on this buffer and places it's starting
    point at the same place as reader r. r MUST be a pointer to a reader
    previous allocated from this buffer.

  */
  IOBufferReader *clone_reader(IOBufferReader *r);

  /**
    Deallocates reader e from this buffer. e MUST be a pointer to a reader
    previous allocated from this buffer. Reader need to allocated when a
    particularly consumer is being removed from the buffer but the buffer
    is still in use. Deallocation is not necessary when the buffer is
    being freed as all outstanding readers are automatically deallocated.

  */
  void dealloc_reader(IOBufferReader *e);

  /**
    Deallocates all outstanding readers on the buffer.

  */
  void dealloc_all_readers();

  void set(void *b, int64_t len);
  void set_xmalloced(void *b, int64_t len);
  void alloc(int64_t i = default_large_iobuffer_size);
  void alloc_xmalloc(int64_t buf_size);
  void append_block_internal(IOBufferBlock *b);
  int64_t write(IOBufferBlock const *b, int64_t len, int64_t offset);
  int64_t puts(char *buf, int64_t len);

  // internal interface

  bool
  empty()
  {
    return !_writer;
  }
  int64_t max_read_avail();

  int max_block_count();
  void check_add_block();

  IOBufferBlock *get_current_block();

  void
  reset()
  {
    if (_writer) {
      _writer->reset();
    }
    for (auto &reader : readers) {
      if (reader.allocated()) {
        reader.reset();
      }
    }
  }

  void
  init_readers()
  {
    for (auto &reader : readers) {
      if (reader.allocated() && !reader.block) {
        reader.block = _writer;
      }
    }
  }

  void
  dealloc()
  {
    _writer = nullptr;
    dealloc_all_readers();
  }

  void
  clear()
  {
    dealloc();
    size_index = BUFFER_SIZE_NOT_ALLOCATED;
    water_mark = 0;
  }

  void
  realloc(int64_t i)
  {
    _writer->realloc(i);
  }
  void
  realloc(void *b, int64_t buf_size)
  {
    _writer->realloc(b, buf_size);
  }
  void
  realloc_xmalloc(void *b, int64_t buf_size)
  {
    _writer->realloc_xmalloc(b, buf_size);
  }
  void
  realloc_xmalloc(int64_t buf_size)
  {
    _writer->realloc_xmalloc(buf_size);
  }

  int64_t size_index;

  /**
    Determines when to stop writing or reading. The watermark is the
    level to which the producer (filler) is required to fill the buffer
    before it can expect the reader to consume any data.  A watermark
    of zero means that the reader will consume any amount of data,
    no matter how small.

  */
  int64_t water_mark;

  Ptr<IOBufferBlock> _writer;
  IOBufferReader readers[MAX_MIOBUFFER_READERS];

#ifdef TRACK_BUFFER_USER
  const char *_location;
#endif

  MIOBuffer(void *b, int64_t bufsize, int64_t aWater_mark);
  MIOBuffer(int64_t default_size_index);
  MIOBuffer();
  ~MIOBuffer();
};

/**
  A wrapper for either a reader or a writer of an MIOBuffer.

*/
struct MIOBufferAccessor {
  IOBufferReader *
  reader()
  {
    return entry;
  }

  MIOBuffer *
  writer()
  {
    return mbuf;
  }

  int64_t
  block_size() const
  {
    return mbuf->block_size();
  }

  int64_t
  total_size() const
  {
    return block_size();
  }

  void reader_for(IOBufferReader *abuf);
  void reader_for(MIOBuffer *abuf);
  void writer_for(MIOBuffer *abuf);

  void
  clear()
  {
    mbuf  = nullptr;
    entry = nullptr;
  }

  MIOBufferAccessor()
    :
#ifdef DEBUG
      name(nullptr),
#endif
      mbuf(nullptr),
      entry(nullptr)
  {
  }

  ~MIOBufferAccessor();

#ifdef DEBUG
  const char *name;
#endif

  // noncopyable
  MIOBufferAccessor(const MIOBufferAccessor &) = delete;
  MIOBufferAccessor &operator=(const MIOBufferAccessor &) = delete;

private:
  MIOBuffer *mbuf;
  IOBufferReader *entry;
};

extern MIOBuffer *new_MIOBuffer_internal(
#ifdef TRACK_BUFFER_USER
  const char *loc,
#endif
  int64_t size_index = default_large_iobuffer_size);

#ifdef TRACK_BUFFER_USER
class MIOBuffer_tracker
{
  const char *loc;

public:
  MIOBuffer_tracker(const char *_loc) : loc(_loc) {}
  MIOBuffer *
  operator()(int64_t size_index = default_large_iobuffer_size)
  {
    return new_MIOBuffer_internal(loc, size_index);
  }
};
#endif

extern MIOBuffer *new_empty_MIOBuffer_internal(
#ifdef TRACK_BUFFER_USER
  const char *loc,
#endif
  int64_t size_index = default_large_iobuffer_size);

#ifdef TRACK_BUFFER_USER
class Empty_MIOBuffer_tracker
{
  const char *loc;

public:
  Empty_MIOBuffer_tracker(const char *_loc) : loc(_loc) {}
  MIOBuffer *
  operator()(int64_t size_index = default_large_iobuffer_size)
  {
    return new_empty_MIOBuffer_internal(loc, size_index);
  }
};
#endif

/// MIOBuffer allocator/deallocator
#ifdef TRACK_BUFFER_USER
#define new_MIOBuffer MIOBuffer_tracker(RES_PATH("memory/IOBuffer/"))
#define new_empty_MIOBuffer Empty_MIOBuffer_tracker(RES_PATH("memory/IOBuffer/"))
#else
#define new_MIOBuffer new_MIOBuffer_internal
#define new_empty_MIOBuffer new_empty_MIOBuffer_internal
#endif
extern void free_MIOBuffer(MIOBuffer *mio);
//////////////////////////////////////////////////////////////////////

extern IOBufferBlock *new_IOBufferBlock_internal(
#ifdef TRACK_BUFFER_USER
  const char *loc
#endif
);

extern IOBufferBlock *new_IOBufferBlock_internal(
#ifdef TRACK_BUFFER_USER
  const char *loc,
#endif
  IOBufferData *d, int64_t len = 0, int64_t offset = 0);

#ifdef TRACK_BUFFER_USER
class IOBufferBlock_tracker
{
  const char *loc;

public:
  IOBufferBlock_tracker(const char *_loc) : loc(_loc) {}
  IOBufferBlock *
  operator()()
  {
    return new_IOBufferBlock_internal(loc);
  }
  IOBufferBlock *
  operator()(Ptr<IOBufferData> &d, int64_t len = 0, int64_t offset = 0)
  {
    return new_IOBufferBlock_internal(loc, d.get(), len, offset);
  }
};
#endif

/// IOBufferBlock allocator
#ifdef TRACK_BUFFER_USER
#define new_IOBufferBlock IOBufferBlock_tracker(RES_PATH("memory/IOBuffer/"))
#else
#define new_IOBufferBlock new_IOBufferBlock_internal
#endif
////////////////////////////////////////////////////////////

extern IOBufferData *new_IOBufferData_internal(
#ifdef TRACK_BUFFER_USER
  const char *location,
#endif
  int64_t size_index = default_large_iobuffer_size, AllocType type = DEFAULT_ALLOC);

extern IOBufferData *new_xmalloc_IOBufferData_internal(
#ifdef TRACK_BUFFER_USER
  const char *location,
#endif
  void *b, int64_t size);

extern IOBufferData *new_constant_IOBufferData_internal(
#ifdef TRACK_BUFFER_USER
  const char *locaction,
#endif
  void *b, int64_t size);

#ifdef TRACK_BUFFER_USER
class IOBufferData_tracker
{
  const char *loc;

public:
  IOBufferData_tracker(const char *_loc) : loc(_loc) {}
  IOBufferData *
  operator()(int64_t size_index = default_large_iobuffer_size, AllocType type = DEFAULT_ALLOC)
  {
    return new_IOBufferData_internal(loc, size_index, type);
  }
};
#endif

#ifdef TRACK_BUFFER_USER
#define new_IOBufferData IOBufferData_tracker(RES_PATH("memory/IOBuffer/"))
#define new_xmalloc_IOBufferData(b, size) new_xmalloc_IOBufferData_internal(RES_PATH("memory/IOBuffer/"), (b), (size))
#define new_constant_IOBufferData(b, size) new_constant_IOBufferData_internal(RES_PATH("memory/IOBuffer/"), (b), (size))
#else
#define new_IOBufferData new_IOBufferData_internal
#define new_xmalloc_IOBufferData new_xmalloc_IOBufferData_internal
#define new_constant_IOBufferData new_constant_IOBufferData_internal
#endif

extern int64_t iobuffer_size_to_index(int64_t size, int64_t max = max_iobuffer_size);
extern int64_t index_to_buffer_size(int64_t idx);
/**
  Clone a IOBufferBlock chain. Used to snarf a IOBufferBlock chain
  w/o copy.

  @param b head of source IOBufferBlock chain.
  @param offset # bytes in the beginning to skip.
  @param len bytes to copy from source.
  @return ptr to head of new IOBufferBlock chain.

*/
extern IOBufferBlock *iobufferblock_clone(IOBufferBlock *b, int64_t offset, int64_t len);
/**
  Skip over specified bytes in chain. Used for dropping references.

  @param b head of source IOBufferBlock chain.
  @param poffset originally offset in b, finally offset in returned
    IOBufferBlock.
  @param plen value of write is subtracted from plen in the function.
  @param write bytes to skip.
  @return ptr to head of new IOBufferBlock chain.

*/
extern IOBufferBlock *iobufferblock_skip(IOBufferBlock *b, int64_t *poffset, int64_t *plen, int64_t write);

inline IOBufferChain &
IOBufferChain::operator=(self_type const &that)
{
  _head = that._head;
  _tail = that._tail;
  _len  = that._len;
  return *this;
}

inline IOBufferChain &
IOBufferChain::operator+=(self_type const &that)
{
  if (nullptr == _head)
    *this = that;
  else {
    _tail->next = that._head;
    _tail       = that._tail;
    _len += that._len;
  }
  return *this;
}

inline int64_t
IOBufferChain::length() const
{
  return _len;
}

inline IOBufferBlock const *
IOBufferChain::head() const
{
  return _head.get();
}

inline IOBufferBlock *
IOBufferChain::head()
{
  return _head.get();
}

inline void
IOBufferChain::clear()
{
  _head = nullptr;
  _tail = nullptr;
  _len  = 0;
}

inline IOBufferChain::const_iterator::const_iterator(self_type const &that) : _b(that._b) {}

inline IOBufferChain::const_iterator &
IOBufferChain::const_iterator::operator=(self_type const &that)
{
  _b = that._b;
  return *this;
}

inline bool
IOBufferChain::const_iterator::operator==(self_type const &that) const
{
  return _b == that._b;
}

inline bool
IOBufferChain::const_iterator::operator!=(self_type const &that) const
{
  return _b != that._b;
}

inline IOBufferChain::const_iterator::value_type &IOBufferChain::const_iterator::operator*() const
{
  return *_b;
}

inline IOBufferChain::const_iterator::value_type *IOBufferChain::const_iterator::operator->() const
{
  return _b;
}

inline IOBufferChain::const_iterator &
IOBufferChain::const_iterator::operator++()
{
  _b = _b->next.get();
  return *this;
}

inline IOBufferChain::const_iterator
IOBufferChain::const_iterator::operator++(int)
{
  self_type pre{*this};
  ++*this;
  return pre;
}

inline IOBufferChain::iterator::value_type &IOBufferChain::iterator::operator*() const
{
  return *_b;
}

inline IOBufferChain::iterator::value_type *IOBufferChain::iterator::operator->() const
{
  return _b;
}
