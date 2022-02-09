// SPDX-License-Identifier: Apache-2.0
// Copyright Verizon Media 2020
/** @file
 * @c BufferWriter for a @c MemArena.
 */
#pragma once

#include "swoc/swoc_version.h"
#include "swoc/MemSpan.h"
#include "swoc/bwf_base.h"
#include "swoc/MemArena.h"

namespace swoc { inline namespace SWOC_VERSION_NS {
/** Buffer writer for a @c MemArena.
 *
 * This provides formatted output to the remnant of a @c MemArena. The output resides in uncommitted
 * arena memory and must be committed externally. This will resize the remnant as needed to contain
 * the output without overflow. Because it uses the remnant, if there is an error or resizing,
 * no arena memory will be lost.
 */
class ArenaWriter : public FixedBufferWriter {
  using self_type  = ArenaWriter;       ///< Self reference type.
  using super_type = FixedBufferWriter; ///< Parent type.
public:
  /** Constructor.
   *
   * @param arena Arena to use for storage.
   */
  ArenaWriter(MemArena &arena);

  /** Write data to the buffer.
   *
   * @param data Data to write.
   * @param n Amount of data in bytes.
   * @return @a this
   */
  ArenaWriter &write(void const *data, size_t n) override;

  /// Write a single character @a c to the buffer.
  ArenaWriter &write(char c) override;

  using super_type::write; // import super class write.

  /** Mark bytes as in use.
   *
   * @param n Number of bytes to include in the used buffer.
   * @return @c true if successful, @c false if additional buffer space was required.
   */
  bool commit(size_t n) override;

protected:
  MemArena &_arena; ///< Arena for the buffer.

  /** Reallocate the buffer to increase the capacity.
   *
   * @param n Total size required.
   */
  void realloc(size_t n);
};

inline swoc::ArenaWriter::ArenaWriter(swoc::MemArena &arena) : super_type(arena.remnant()), _arena(arena) {}

}} // namespace swoc::SWOC_VERSION_NS
