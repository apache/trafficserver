/** @file

    TS Configuration API implementation.

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

# include "TsValue.h"
# include "TsBuilder.h"
# include "ts/ink_defs.h"

# include <TsErrataUtil.h>
# include <sys/stat.h>
# include <cstdio>
# include <cstdlib>

# if !defined(_MSC_VER)
# define _fileno fileno
# endif

// ---------------------------------------------------------------------------
namespace ts { namespace config {
// ---------------------------------------------------------------------------
Buffer const detail::NULL_BUFFER;
ConstBuffer const detail::NULL_CONST_BUFFER;
detail::ValueItem detail::ValueTableImpl::NULL_ITEM(VoidValue);
detail::PseudoBool::Type const detail::PseudoBool::FALSE = nullptr;
detail::PseudoBool::Type const detail::PseudoBool::TRUE = &detail::PseudoBool::operator !;
// This should not be called, it is used only as a pointer value.
bool detail::PseudoBool::operator ! () const { return false; }
// ---------------------------------------------------------------------------
unsigned int const detail::Type_Property[N_VALUE_TYPES] = {
  0, // Void
  detail::IS_VALID | detail::IS_CONTAINER, // List
  detail::IS_VALID | detail::IS_CONTAINER, // Group
  detail::IS_VALID | detail::IS_LITERAL, // String
  detail::IS_VALID | detail::IS_LITERAL, // Integer
};
// ---------------------------------------------------------------------------
detail::ValueTableImpl::ValueTableImpl() : _generation(0) { }
detail::ValueTableImpl::~ValueTableImpl() {
  for (auto & _buffer : _buffers) {
    free(_buffer._ptr);
}
}
// ---------------------------------------------------------------------------
detail::ValueTable::ImplType*
detail::ValueTable::instance() {
  // Stupid workaround for clang analyzer false positive. The new instance isn't leaked because
  // it's stored in a shared ptr type.
#if !defined(__clang_analyzer__)
  if (! _ptr) { _ptr.reset(new ImplType); }
#else
  assert(_ptr.get() != nullptr);
#endif
  return _ptr.get();
}

detail::ValueTable&
detail::ValueTable::forceRootItem() {
  ImplType* imp = this->instance();
  if (0 == imp->_values.size()) {
    imp->_values.push_back(ValueItem(GroupValue));
}
  return *this;
}

Rv<detail::ValueIndex>
detail::ValueTable::make(ValueIndex pidx, ValueType type, ConstBuffer const& name) {
  Rv<ValueIndex> zret = NULL_VALUE_INDEX;
  if (_ptr) {
    size_t n = _ptr->_values.size();
    // Check the parent
    if (pidx < n) {
      ValueItem* parent = &(_ptr->_values[pidx]);
      if (IS_CONTAINER & Type_Property[parent->_type]) {
        ValueItem* item;

        _ptr->_values.push_back(ValueItem(type));
        parent = &(_ptr->_values[pidx]); // possibly stale, refresh.
        item = &(_ptr->_values[n]);
        item->_parent = pidx;
        parent->_children.push_back(n);
        item->_local_index = parent->_children.size() - 1;
        // Only use the name if the parent is a group.
        if (GroupValue == parent->_type) { item->_name = name;
}
        zret = n; // mark for return to caller.
      } else {
        msg::log(zret.errata(), msg::WARN, "Add child failed because parent is not a container.");
      }
    } else {
      msg::logf(zret.errata(), msg::WARN, "Add child failed because parent index (%ul) is out of range (%ul).", pidx.raw(), n);
    }
  } else {
    msg::log(zret.errata(), msg::WARN, "Add child failed because the configuration is null.");
  }
  return zret;
}

detail::ValueItem&
detail::ValueTable::operator [] ( ValueIndex idx ) {
  assert(_ptr && idx < _ptr->_values.size());
  return _ptr->_values[idx];
}
Buffer
detail::ValueTable::alloc(size_t n) {
  ImplType* imp = this->instance();
  Buffer zret(static_cast<char*>(malloc(n)), n);
  if (zret._ptr) { imp->_buffers.push_back(zret);
}
  return zret;
}

// ---------------------------------------------------------------------------
Value
Value::operator [] (size_t idx) const {
  Value zret;
  detail::ValueItem const* item = this->item();
  if (item && idx < item->_children.size()) {
    zret = Value(_config, item->_children[idx]);
    if (PathValue == zret.getType()) { zret = _config.getRoot().find(_config._table[zret._vidx]._path);
}
  }
  return zret;
}

Value
Value::operator [] (ConstBuffer const& name) const {
  Value zret;
  detail::ValueItem const* item = this->item();
  if (item) {
    for (const auto & spot : item->_children) {
      if (_config._table[spot]._name == name) {
        zret = Value(_config, spot);
        if (PathValue == zret.getType()) { zret = _config.getRoot().find(_config._table[zret._vidx]._path);
}
        break;
      }
    }
  }
  return zret;
}

Value
Value::find( ConstBuffer const& path ) {
  Value zret = *this;
  Path::Parser parser(path);
  Rv<Path::Parser::Result> x;
  ConstBuffer elt;
  for ( x = parser.parse(&elt) ; zret && Path::Parser::EOP != x && Path::Parser::ERROR != x ; x = parser.parse(&elt) ) {
    if (Path::Parser::TAG == x) { zret = zret[elt];
    } else if (Path::Parser::INDEX == x) { zret = zret[elt._size];
    } else { zret.reset();
}
  }
  if (Path::Parser::EOP != x) { zret.reset();
}
  return zret;
}

Value
Value::find(Path const& path ) {
  Value zret = *this;
  for ( size_t i = 0, n = path.count() ; i < n && zret ; ++i ) {
    ConstBuffer const& elt = path[i];
    if (elt._ptr) { zret = zret[elt];
    } else { zret = zret[elt._size];
}
  }
  return zret;
}

Rv<Value>
Value::makeChild(ValueType type, ConstBuffer const& name) {
  Rv<Value> zret;
  Rv<detail::ValueIndex> vr = _config._table.make(this->_vidx, type, name);
  if (vr.isOK()) { zret = Value(_config, vr.result());
  } else { zret.errata() = vr.errata();
}
  return zret;
}

Rv<Value>
Value::makeGroup(ConstBuffer const& name) {
  return this->makeChild(GroupValue, name);
}

Rv<Value>
Value::makeList(ConstBuffer const& name) {
  return this->makeChild(ListValue, name);
}

Rv<Value>
Value::makeString(ConstBuffer const& text, ConstBuffer const& name) {
  Rv<Value> zret = this->makeChild(StringValue, name);
  if (zret.isOK()) { zret.result().setText(text);
}
  return zret;
}

Rv<Value>
Value::makeInteger(ConstBuffer const& text, ConstBuffer const& name) {
  Rv<Value> zret = this->makeChild(IntegerValue, name);
  if (zret.isOK()) { zret.result().setText(text);
}
  return zret;
}

Rv<Value>
Value::makePath(Path const& path, ConstBuffer const& name) {
  Rv<Value> zret = this->makeChild(PathValue, name);
  if (zret.isOK()) { _config._table[zret.result()._vidx]._path = path;
}
  return zret;
}
// ---------------------------------------------------------------------------
Path& Path::reset() {
  if (_ptr) {
    // If we're sharing the instance, make a new one for us.
    if (_ptr.use_count() > 1) {
      _ptr.reset(new ImplType);
    } else { // clear out the existing instance.
      _ptr->_elements.clear();
    }
  }
  return *this;
}
// ---------------------------------------------------------------------------
Rv<Path::Parser::Result>
Path::Parser::parse(ConstBuffer *cbuff) {
  Rv<Result> zret = EOP;
  enum State {
    S_INIT, // initial state
    S_INDEX, // reading index.
    S_TAG, // reading tag.
    S_DASH, // reading dashes in tag.
  } state = S_INIT;

  // Character bucket
  enum Bucket {
    C_INVALID, // Invalid input.
    C_DIGIT, // digit.
    C_IDENT, // Identifier character.
    C_DASH, // A dash
    C_DOT, // A dot (period).
  };

  if (cbuff) { cbuff->reset();
}
  char const* start = _c; // save starting character location.
  size_t idx = 0; // accumulator for index value.

  bool final = false;
  while (! final && this->hasInput()) {
    Bucket cb;
    if (isdigit(*_c)) { cb = C_DIGIT;
    } else if ('_' == *_c || isalpha(*_c)) { cb = C_IDENT;
    } else if ('-' == *_c) { cb = C_DASH;
    } else if ('.' == *_c) { cb = C_DOT;
    } else { cb = C_INVALID;
}

    if (C_INVALID == cb) {
      msg::logf(zret, msg::WARN, "Invalid character '%c' [%u] in path.", *_c, *_c);
    } else { switch (state) {
      case S_INIT:
        switch (cb) {
        case C_DIGIT: state = S_INDEX; idx = *_c - '0'; break;
        case C_IDENT: state = S_TAG; break;
        case C_DASH: msg::logf(zret, msg::WARN, "Dash not allowed as leading character for tag."); final = true; break;
        case C_DOT: msg::logf(zret, msg::WARN, "Separator without preceding element."); final = true; break;
        default: msg::logf(zret, msg::WARN, "Internal error: unexpected character %u in INIT state.", *_c); final = true; break;
        }
        break;
      case S_INDEX: // reading an index.
        if (C_DIGIT == cb) { idx = 10 * idx + *_c - '0';
        } else if (C_DOT == cb) { final = true; }
        else {
          msg::logf(zret, msg::WARN, "Invalid character '%c' [%u] in index element.", *_c, *_c);
          final = true;
        }
        break;
      case S_TAG: // reading a tag.
        if (C_IDENT == cb || C_DIGIT == cb) { ; // continue
        } else if (C_DASH == cb) { state = S_DASH;
        } else if (C_DOT == cb) { final = true; }
        else { // should never happen, but be safe.
          msg::logf(zret, msg::WARN, "Invalid character '%c' [%u] in index element.", *_c, *_c);
          final = true;
        }
        break;
      case S_DASH: // dashes inside tag.
        if (C_IDENT == cb || C_DIGIT == cb) { state = S_TAG;
        } else if (C_DOT == cb) {
          msg::log(zret, msg::WARN, "Trailing dash not allowed in tag element.");
          final = true;
        } else if (C_DASH != cb) { // should never happen, but be safe.
          msg::logf(zret, msg::WARN, "Invalid character '%c' [%u] in index element.", *_c, *_c);
          final = true;
        }
        break;
      }
}
    ++_c;
  }
  if (!zret.isOK()) {
    zret = ERROR;
    if (cbuff) { cbuff->set(_c - 1, 1);
}
    _c = nullptr;
    _input.reset();
  } else if (S_INIT == state) {
    zret = EOP;
  } else if (S_TAG == state) {
    zret = TAG;
    if (cbuff) {
      cbuff->set(start, _c - start);
      // if @a final is set, then we parsed a dot separator.
      // don't include it in the returned tag.
      if (final) { cbuff->_size -= 1;
}
    }
  } else if (S_INDEX == state) {
    zret = INDEX;
    if (cbuff) { cbuff->_size = idx;
}
  } else if (S_DASH == state) {
    zret = ERROR;
    msg::log(zret, msg::WARN, "Trailing dash not allowed in tag element.");
    if (cbuff) { cbuff->set(start, _c - start);
}
  }
  return zret;
}
// ---------------------------------------------------------------------------
Value
Configuration::getRoot() const {
  const_cast<self*>(this)->_table.forceRootItem();
  return Value(*this, 0);
}

Rv<Configuration>
Configuration::loadFromPath(char const* path) {
  Rv<Configuration> zret;
  Buffer buffer;
  FILE* in = fopen(path, "r");

  if (in) {
    struct stat info;
    if (0 == fstat(_fileno(in), &info)) {
      // Must reserve 2 bytes at the end for FLEX terminator.
      buffer = zret.result().alloc(info.st_size + 2);
      if (buffer._ptr) {
        size_t n;
        if (0 < (n = fread(buffer._ptr, sizeof(char), info.st_size, in))) {
          buffer._size = n+2;
          memset(buffer._ptr + n, 0, 2); // required by FLEX
          zret = Builder(zret.result()).build(buffer);
        } else {
          msg::logf_errno(zret, msg::WARN, "failed to read %" PRIu64 " bytes from configuration file '%s'", info.st_size, path);
        }
      } else {
        msg::logf_errno(zret, msg::WARN, "failed to allocate buffer for configuration file '%s' - needed %" PRIu64 " bytes.", path, info.st_size);
      }
    } else {
      msg::logf_errno(zret, msg::WARN, "failed to determine file information on '%s'", path);
    }
    fclose(in);
  } else {
    msg::logf_errno(zret, msg::WARN, "failed to open configuration file '%s'", path);
  }
  return zret;
}

}} // namespace ts::config
