#pragma once
#include "stdint.h"
#include <cstddef>
#include <atomic>
#include <vector>

#ifndef TSAssert
#define TSAssert(...)
#endif

/// runtime defined static container
/** The size of this structure is actually zero, so it will not change the size of your derived class.
 * But new and delete are overriden to use allocate enough bytes of the derived type + properties added
 * at run time using ioBufferAllocator.
 *
 * Also all bools are packed to save space using the *Bit methods.
 *
 * This is templated so static variables are instanced per Derived type. B/c we need to have different
 * size property sets.
 */
template <class Derived_t> ///< static members instance pey type.
struct PropertyBlock {
public:
  /// callback format used to PropBlock init and destroy
  using PropertyFunc_t = void(PropertyBlock<Derived_t> *, void *);

  /// number of bytes from 'this', or the bitset index
  using Offset_t = uint32_t;

  /// Add a data type to schema.
  /**
   * @param prop_count the number of instances of this data type to allocate.
   * @param init an initialization function for property
   * @param destroy a destruction function for property
   * Note: if prop_count is 0, the init and destroy are still called per instance.
   */
  template <typename Property_t>
  static Offset_t
  PropBlockDeclare(size_t prop_count, PropertyFunc_t init, PropertyFunc_t destroy)
  {
    TSAssert(s_instance_count == 0); // it's too late, we already started allocating.
    static_assert(typeid(Property_t) != typeid(bool),
                  "Use PropBlockDeclareBit so we can pack bits, and we can't return a pointer to a bit.");

    Offset_t offset = s_properties_total_size + sizeof(Derived_t);
    s_properties_total_size += prop_count * sizeof(Property_t);
    if (init || destroy) {
      do {
        m_schema.insert(Block{offset, init, destroy});
      } while (--prop_count > 0);
    }
    return offset;
  }

  /// returns a pointer at the given offset
  template <typename Property_t>
  Property_t *
  propBlockGet(Offset_t offset)
  {
    TSAssert(Derived_t::canAccess(this));           // the Derived_t probably needs a thread lock
    TSAssert(propBlockGetBit(initialized) == true); // the Derived_t needs to call propBlockInit() during construction.
    return static_cast<Property_t *>(((char *)this) + offset);
  }

  static Offset_t PropBlockDeclareBit(int bit_count = 1) // all bits init to 0. there is no init & destroy.
  {
    TSAssert(s_instance_count == 0); // it's too late, we already started allocating.
    Offset_t offset = s_bits_size;
    s_bits_size += bit_count;
    return offset;
  }

  bool
  propBlockGetBit(Offset_t offset)
  {
    TSAssert(Derived_t::canAccess(this));
    return propBlockGet<char>(s_properties_total_size + offset / 8) & (1 << (offset % 8));
  }
  void
  propBlockPutBit(Offset_t offset, bool val)
  {
    TSAssert(Derived_t::canAccess(this));
    char &c = propBlockGet<char>(s_properties_total_size + offset / 8);
    if (val) {
      c |= (1 << (offset % 8));
    } else {
      c &= ~(1 << (offset % 8));
    }
  }

  ///
  /// lifetime management
  ///
  /** If you have to override the new/delete of the derived class be sure to
   * call these PropertyBlock::alloc(sz) and PropertyBlock::free(ptr)
   */

  /// allocate a new object with properties
  void *
  operator new(size_t size)
  {
    // allocate one block
    const size_t alloc_size = size + s_properties_total_size + (s_bits_size + 7) / 8;
    void *ptr               = ::operator new(alloc_size);
    memset(ptr, 0, alloc_size);

    return ptr;
  }

  PropertyBlock() { s_instance_count++; }
  // we have to all propBlockInit after the Derived contructor. IDK how to do that implicitly.

  ~PropertyBlock()
  {
    s_instance_count++;
    propBlockDestroy();
  }

protected:
  void
  propBlockInit()
  {
    // is already constructed?
    if (propBlockGetBit(initialized) == true) {
      return;
    }
    propBlockPutBit(initialized, true);
    for (Block &block : m_schema) {
      if (block.m_init) {
        block.m_init(this, PropBlockGet(block.m_offset));
      }
    }
  }

  void
  propBlockDestroy()
  {
    // is already destroyed?
    if (propBlockGetBit(initialized) == false) {
      return;
    }
    propBlockPutBit(initialized, false);
    for (Block &block : m_schema) {
      if (block.m_destroy) {
        block.m_destroy(this, PropBlockGet(block.m_offset));
      }
    }
  }

private:
  /// total size of all declared property structures
  static uint32_t s_properties_total_size;

  enum status_bits {
    initialized, // true if the propBlock is initialized, false if destroyed.
    num_status_bits
  };
  /// total bits of all declared propertyBits
  static uint32_t s_bits_size;

  /// the number of class instances in use.
  static std::atomic_uint s_instance_count;

  struct Block {
    Offset_t m_offset;
    PropertyFunc_t m_init;    ///< called after new
    PropertyFunc_t m_destroy; ///< called before delete

    // if we need to save and load, then we should add names to the blocks.
  };
  static std::vector<Block> m_schema;
};

/// total size of all declared property structures
template <class Derived_t> uint32_t PropertyBlock<Derived_t>::s_properties_total_size = 0;

/// total bits of all declared propertyBits
template <class Derived_t> uint32_t PropertyBlock<Derived_t>::s_bits_size = 0;

/// the number of class instances in use.
template <class Derived_t> std::atomic_uint PropertyBlock<Derived_t>::s_instance_count{0};