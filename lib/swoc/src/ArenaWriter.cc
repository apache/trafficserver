// SPDX-License-Identifier: Apache-2.0
// Copyright Verizon Media 2020
/** @file
 * @c BufferWriter for a @c MemArena.
 */

#include "swoc/ArenaWriter.h"

namespace swoc { inline namespace SWOC_VERSION_NS {

ArenaWriter &
ArenaWriter::write(char c) {
  if (_attempted >= _capacity) {
    this->realloc(_attempted + 1);
  }
  this->super_type::write(c);
  return *this;
}

ArenaWriter &
ArenaWriter::write(void const *data, size_t n) {
  if (n + _attempted > _capacity) {
    this->realloc(n + _attempted);
  }
  this->super_type::write(data, n);
  return *this;
}

bool
ArenaWriter::commit(size_t n) {
  if (_attempted + n > _capacity) {
    this->realloc(_attempted + n);
    return false;
  }
  return this->super_type::commit(n);
}

void
ArenaWriter::realloc(size_t n) {
  auto text                    = this->view(); // Current data.
  auto span                    = _arena.require(n).remnant().rebind<char>();
  const_cast<char *&>(_buffer) = span.data();
  _capacity                    = span.size();
  memcpy(_buffer, text.data(), text.size());
}

}} // namespace swoc::SWOC_VERSION_NS
