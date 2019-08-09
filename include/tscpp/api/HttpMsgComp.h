/**
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

/**
 * @file HttpMsgComp.h
 *
 * Classes for convenient manipulation of HTTP message components.
 */

/*

NOTES
-----

The classes in this include file may be used independently from the rest of the C++ API.

These clases are designed to create instances as local variables in functions.  In TS API hook handling code,
no TS API function may be called after the call to TSHttpTxnReenable() (which is called by the resume() and error()
member functions of atscppapi::Transaction).  TS API functions are called by the destructors of these classes.
Therefore, blocks containing instances of these claesses must end before the call to TSHttpTxnReenable().

void * is used as the formal parameter type in function prototypes when the actual parameter type should be
TSHttpTxn (a transaction handle).  This is for compatability with  atscppapi::Transaction::getAtsHandle().

A (non-null) TSMLoc may point to 4 different types of objects:
- An HTTP message.
- The URL in a HTTP Request message.
- The MIME header in an HTTP message.
- A field (line) in a MIME header.
Calling TSHamdleMLocRelease() is optional (does nothing) except when the TSMLoc points to a MIME header field.
This code does not make the optional calls to TSHandleMLocRelease().

This example indicates that the optimizer can be expected to avoid unnecessary copying of the TSMBuffer and
TSMLoc for the HTTP message:  https://godbolt.org/z/bBZ03T

*/

#pragma once

#include <string_view>
#include <utility>
#include <memory>

#include <ts/ts.h>

// Avoid conflicts with system include files.
//
#undef major
#undef minor

// Use this assert() when the checked expression has no side effects that are needed in the release build.  Otherwise,
// use TSAssert().
//
#undef assert
#if defined(__OPTIMIZE__)
#define assert(EXPR) static_cast<void>(0)
#else
#define assert TSAssert
#endif

namespace atscppapi
{
namespace detail
{
  // Functor.
  //
  struct CallTSFree {
    void
    operator()(char *p)
    {
      if (p) {
        TSfree(p);
      }
    }
  };
} // end namespace detail

// Class whose instances own char arrays which should be freed with TSfree().  (The array is freed by the
// destructor).
//
class DynamicCharArray
{
public:
  DynamicCharArray() : _length(0) {}

  DynamicCharArray(char *arr, int length) : _arr(arr), _length(length) {}

  explicit operator bool() { return static_cast<bool>(_arr); }

  char *
  data()
  {
    return _arr.get();
  }

  int
  length() const
  {
    return _length;
  }

  std::string_view
  asStringView() const
  {
    return std::string_view(_arr.get(), _length);
  }

  void
  reset()
  {
    _arr.reset();
    _length = 0;
  }

private:
  std::unique_ptr<char[], detail::CallTSFree> _arr;
  int _length;
};

namespace detail
{
  template <TSReturnCode (*txnRemapUrlGetFuncPtr)(TSHttpTxn, TSMLoc *)>
  DynamicCharArray
  txnRemapUrlStringGet(void *txn)
  {
    TSMLoc urlLoc;
    if (txnRemapUrlGetFuncPtr(static_cast<TSHttpTxn>(txn), &urlLoc) != TS_SUCCESS) {
      return DynamicCharArray(nullptr, 0);
    }
    int length;
    char *str = TSUrlStringGet(nullptr, urlLoc, &length);
    return DynamicCharArray(str, str ? length : 0);
  }
} // end namespace detail

DynamicCharArray
txnRemapFromUrlStringGet(void *txn)
{
  return detail::txnRemapUrlStringGet<TSRemapFromUrlGet>(txn);
}

DynamicCharArray
txnRemapToUrlStringGet(void *txn)
{
  return detail::txnRemapUrlStringGet<TSRemapToUrlGet>(txn);
}

// Note:  The TSUrlXxxGet() functions do not work for the remap to/from URLs.  That is why there are no equivalent
// capability in C++ provided in this header file.

// This function returns the "effective" URL for the client request HTTP message that triggered the (given)
// transaction.  "Effective" means that, if the URL in the request was mearly a path, this function returns the
// equivalent absolute URL.  This function does NOT normalize the host to lower case letters.
//
DynamicCharArray
txnEffectiveUrlStringGet(void *txn)
{
  int length;
  char *str = TSHttpTxnEffectiveUrlStringGet(static_cast<TSHttpTxn>(txn), &length);
  return DynamicCharArray(str, str ? length : 0);
}

using MsgBuffer = TSMBuffer;

class HttpVersion
{
public:
  HttpVersion(int major, int minor) : _v((major << 16) | minor) {}

  explicit HttpVersion(int raw) : _v(raw) {}

  int
  major() const
  {
    return _v >> 16;
  }

  int
  minor() const
  {
    return _v & ((1 << 16) - 1);
  }

  int
  raw() const
  {
    return _v;
  }

private:
  int _v;
};

class MsgBase
{
public:
  MsgBase() : _msgBuffer(nullptr), _loc(TS_NULL_MLOC) {}

  MsgBuffer
  msgBuffer() const
  {
    return _msgBuffer;
  }

  TSMLoc
  loc() const
  {
    return _loc;
  }

  bool
  hasMsg() const
  {
    return loc() != TS_NULL_MLOC;
  }

  friend bool
  operator==(const MsgBase &a, const MsgBase &b)
  {
    if ((a._msgBuffer == b._msgBuffer) && (a._loc == b._loc)) {
      return true;
    }

    return !a.hasMsg() && !b.hasMsg();
  }

  friend bool
  operator!=(const MsgBase &a, const MsgBase &b)
  {
    return !(a == b);
  }

  enum class Type { UNKNOWN = TS_HTTP_TYPE_UNKNOWN, REQUEST = TS_HTTP_TYPE_REQUEST, RESPONSE = TS_HTTP_TYPE_RESPONSE };

  Type
  type() const
  {
    assert(hasMsg());

    return static_cast<Type>(TSHttpHdrTypeGet(_msgBuffer, _loc));
  }

  HttpVersion
  httpVersionGet() const
  {
    assert(hasMsg());

    return HttpVersion(TSHttpHdrVersionGet(_msgBuffer, _loc));
  }

  void
  httpVersionSet(HttpVersion v)
  {
    assert(hasMsg());

    TSHttpHdrVersionSet(_msgBuffer, _loc, v.raw());
  }

  //  Returns number of MIME header lines in HTTP message.  Can only be called if hasMsg() is true.
  //
  int
  mimeFieldsCount() const
  {
    assert(hasMsg());

    return TSMimeHdrFieldsCount(msgBuffer(), loc());
  }

  int
  hdrLength() const
  {
    assert(hasMsg());

    return TSHttpHdrLengthGet(msgBuffer(), loc());
  }

protected:
  MsgBase(MsgBuffer msgBuffer, TSMLoc loc) : _msgBuffer(msgBuffer), _loc(loc) {}

private:
  MsgBuffer _msgBuffer;
  TSMLoc _loc;
};

class MimeField
{
public:
  MimeField() : _loc(TS_NULL_MLOC) {}

  MimeField(MsgBase msg, TSMLoc loc) : _msg(msg), _loc(loc) { assert(_msg.hasMsg() && (_loc != TS_NULL_MLOC)); }

  // MimeField at (zero-base) index idx in HTTP message.
  //
  MimeField(MsgBase msg, int idx);

  // (First) MimeField with given name in HTTP message (valid() is false if field with given name not present).
  //
  MimeField(MsgBase msg, std::string_view name);

  // Create new MIME field in message and optionally include a name for it.
  //
  static MimeField create(MsgBase msg, std::string_view name = std::string_view());

  TSMLoc
  loc() const
  {
    return _loc;
  }

  MsgBase
  msg() const
  {
    return _msg;
  }

  // Valid means non-empty.
  //
  bool valid() const;

  // Put instance into empty state, releasing resources as appropriate.
  //
  void reset();

  // Can not copy, only move.

  MimeField(const MimeField &) = delete;
  MimeField &operator=(const MimeField &) = delete;

  MimeField(MimeField &&source);
  MimeField &operator=(MimeField &&source);

  // A Call to this function on an invalid instance is ignored.
  //
  void destroy();

  // Next field, returns invalid instance if none.
  //
  MimeField next() const;

  // Next field with same name, return invalid instance if none.
  //
  MimeField nextDup() const;

  // For the given field name, returns the last Mime field with that name in the given message, or an invalid MimeField if the
  // message contains no field by theat name.
  //
  static MimeField lastDup(MsgBase msg, std::string_view name);

  std::string_view nameGet() const;

  void nameSet(std::string_view);

  // Get a comma-separated list of all values (or single value).  The returned string_view is invalidated by any change
  // to field's list of values.
  //
  std::string_view valuesGet() const;

  // Set comman-separated list of all values (or single value)
  //
  void valuesSet(std::string_view new_values = std::string_view());

  // Append a new value at the end (with separating comma if there are already one or more values).
  //
  void valAppend(std::string_view new_value);

  // NOTE: valuesCount(), valGet(), valSet() and valInsert() should be used rarely.  If you are iterating over the
  // comma-separated values for field, you generally should use the TextView member function take_prefix_at(',').

  // Returns number of values. Values index are from 0 to valuesCount() - 1.
  //
  int valuesCount() const;

  // The returned string_view is invalidated by any change to the field's list of values.
  //
  std::string_view valGet(int idx) const;

  void valSet(int idx, std::string_view new_value = std::string_view());

  // Insert a new value at index idx.  All values with index >= idx prior to calling this have their index incremented by one.
  //
  void valInsert(int idx, std::string_view new_value);

  ~MimeField();

private:
  MsgBase _msg;
  TSMLoc _loc;
};

class ReqMsg : public MsgBase
{
public:
  ReqMsg() {}

  ReqMsg(MsgBuffer msgBuffer, TSMLoc loc) : MsgBase(msgBuffer, loc) { assert(type() == Type::REQUEST); }

  explicit ReqMsg(MsgBase base) : MsgBase(base) { assert(type() == Type::REQUEST); }

  std::string_view
  methodGet() const
  {
    assert(hasMsg());

    int length;
    const char *data = TSHttpHdrMethodGet(msgBuffer(), loc(), &length);

    if (!length || !data) {
      length = 0;
      data   = nullptr;
    }

    return std::string_view(data, length);
  }

  bool
  methodSet(std::string_view sv)
  {
    assert(hasMsg());

    assert(sv.data() && sv.size());

    return TSHttpHdrMethodSet(msgBuffer(), loc(), sv.data(), sv.length()) == TS_SUCCESS;
  }

  // This function returns the "effective" URL for this request HTTP message.  "Effective" means that, if the URL
  // in the request was mearly a path, this function returns the equivalent absolute URL. This function normalizes
  // the host to lower case letters.  This function returns a negative value if the the message does not have a URL,
  // or some other error occurs.  Otherwise, it returns the number of characters in the effective URL.  'buf' must
  // point to an array of char whose dimension is given by 'size'.  If the number of characters in the effective URL
  // is less than 'size', the effective URL wil be copied into 'buf'.  Otherwise, no data will be put into 'buf'.
  //
  int
  effectiveUrl(char *buf, int size) const
  {
    assert(hasMsg());

    int64_t length;
    if (TSHttpHdrEffectiveUrlBufGet(msgBuffer(), loc(), buf, size, &length) == TS_SUCCESS) {
      return static_cast<int>(length);
    }
    return -1;
  }

  // Synonym.
  //
  int
  absoluteUrl(char *buf, int size) const
  {
    return effectiveUrl(buf, size);
  }

  // Get URL loc.  Returns true for success, false for failure.  'urlLoc' will contain TSMLoc for URL, which will
  // be in the TSMBuffer given by msgBuffer() for this instance.
  //
  bool
  urlLocGet(TSMLoc &urlLoc)
  {
    assert(hasMsg());

    return TSHttpHdrUrlGet(msgBuffer(), loc(), &urlLoc) == TS_SUCCESS;
  }

  // Set URL loc to 'urlLoc'.  Returns true for success, false for failure.  'urlLoc' must refer to the TSMBuffer
  // given by msgBuffer() for this instance.
  //
  bool
  urlLocSet(TSMLoc &urlLoc)
  {
    assert(hasMsg());

    return TSHttpHdrUrlSet(msgBuffer(), loc(), urlLoc) == TS_SUCCESS;
  }
};

class RespMsg : public MsgBase
{
public:
  using Status = TSHttpStatus;

  RespMsg() {}

  RespMsg(MsgBuffer msgBuffer, TSMLoc loc) : MsgBase(msgBuffer, loc) { assert(type() == Type::RESPONSE); }

  explicit RespMsg(MsgBase base) : MsgBase(base) { assert(type() == Type::RESPONSE); }

  Status
  statusGet() const
  {
    assert(hasMsg());

    return TSHttpHdrStatusGet(msgBuffer(), loc());
  }

  bool
  statusSet(Status s)
  {
    assert(hasMsg());

    return TSHttpHdrStatusSet(msgBuffer(), loc(), s) == TS_SUCCESS;
  }

  std::string_view
  reasonGet() const
  {
    assert(hasMsg());

    int length;
    const char *data = TSHttpHdrReasonGet(msgBuffer(), loc(), &length);
    if (!length || !data) {
      length = 0;
      data   = nullptr;
    }
    return std::string_view(data, length);
  }

  bool
  reasonSet(std::string_view sv)
  {
    assert(hasMsg());

    assert(sv.data() && sv.size());

    return TSHttpHdrReasonSet(msgBuffer(), loc(), sv.data(), sv.length()) == TS_SUCCESS;
  }
};

// ReqOrReqpMsgBase parameter should be either ReqMsg or RespMsg.
//
template <TSReturnCode (*getInTxn)(TSHttpTxn txnp, MsgBuffer *, TSMLoc *), class ReqOrRespMsgBase>
class TxnMsg : public ReqOrRespMsgBase
{
public:
  TxnMsg() {}

  explicit TxnMsg(void *txnHndl) { _init(txnHndl); }

  // Must not be called on instance that already has a message.
  //
  bool
  init(void *txnHndl)
  {
    assert(!this->hasMsg());

    _init(txnHndl);

    return (this->hasMsg());
  }

private:
  void
  _init(void *txnHndl)
  {
    MsgBuffer msgBuffer;
    TSMLoc loc;

    if (TS_SUCCESS == getInTxn(static_cast<TSHttpTxn>(txnHndl), &msgBuffer, &loc)) {
      *static_cast<MsgBase *>(this) = ReqOrRespMsgBase(msgBuffer, loc);
    }
  }
};

using TxnClientReq  = TxnMsg<TSHttpTxnClientReqGet, ReqMsg>;
using TxnClientResp = TxnMsg<TSHttpTxnClientRespGet, RespMsg>;
using TxnServerReq  = TxnMsg<TSHttpTxnServerReqGet, ReqMsg>;
using TxnServerResp = TxnMsg<TSHttpTxnServerRespGet, RespMsg>;
using TxnCachedReq  = TxnMsg<TSHttpTxnCachedReqGet, ReqMsg>;
using TxnCachedResp = TxnMsg<TSHttpTxnCachedRespGet, RespMsg>;

//////////////////// Inline Member Function Implementations //////////////////

inline MimeField::MimeField(MsgBase msg, int idx) : _msg(msg)
{
  assert(msg.hasMsg());

  assert((idx >= 0) && (idx < msg.mimeFieldsCount()));

  _loc = TSMimeHdrFieldGet(_msg.msgBuffer(), _msg.loc(), idx);
}

inline MimeField::MimeField(MsgBase msg, std::string_view name) : _msg(msg)
{
  assert(msg.hasMsg());

  assert(name.data() && name.size());

  _loc = TSMimeHdrFieldFind(_msg.msgBuffer(), _msg.loc(), name.data(), name.length());
}

inline MimeField
MimeField::create(MsgBase msg, std::string_view name)
{
  assert(msg.hasMsg());

  assert(name.data() && name.size());

  TSMLoc loc;
  if (name != std::string_view()) {
    if (TSMimeHdrFieldCreateNamed(msg.msgBuffer(), msg.loc(), name.data(), name.length(), &loc) != TS_SUCCESS) {
      loc = TS_NULL_MLOC;
    }
  } else {
    if (TSMimeHdrFieldCreate(msg.msgBuffer(), msg.loc(), &loc) != TS_SUCCESS) {
      loc = TS_NULL_MLOC;
    }
  }
  return loc != TS_NULL_MLOC ? MimeField(msg, loc) : MimeField();
}

inline bool
MimeField::valid() const
{
  return _msg.hasMsg() && (TS_NULL_MLOC != _loc);
}

inline void
MimeField::reset()
{
  if (valid()) {
    TSAssert(TSHandleMLocRelease(_msg.msgBuffer(), _msg.loc(), _loc) == TS_SUCCESS);
  }

  _loc = TS_NULL_MLOC;
}

inline MimeField::MimeField(MimeField &&source) : _msg(source._msg)
{
  assert(_msg.hasMsg());

  _loc        = source._loc;
  source._loc = TS_NULL_MLOC;
}

inline MimeField &
MimeField::operator=(MimeField &&source)
{
  if (valid()) {
    // Be OCD and block mf = std::move(mf) in debug loads.
    //
    assert((_msg != source._msg) || (_loc != source._loc));

    TSAssert(TSHandleMLocRelease(_msg.msgBuffer(), _msg.loc(), _loc) == TS_SUCCESS);
  }

  _msg        = source._msg;
  _loc        = source._loc;
  source._loc = TS_NULL_MLOC;

  return *this;
}

inline void
MimeField::destroy()
{
  if (valid()) {
    TSAssert(TSMimeHdrFieldDestroy(_msg.msgBuffer(), _msg.loc(), _loc) == TS_SUCCESS);

    TSAssert(TSHandleMLocRelease(_msg.msgBuffer(), _msg.loc(), _loc) == TS_SUCCESS);

    _loc = TS_NULL_MLOC;
  }
}

inline MimeField
MimeField::next() const
{
  assert(valid());

  TSMLoc nLoc = TSMimeHdrFieldNext(_msg.msgBuffer(), _msg.loc(), _loc);
  return nLoc != TS_NULL_MLOC ? MimeField(_msg, nLoc) : MimeField();
}

inline MimeField
MimeField::nextDup() const
{
  assert(valid());

  TSMLoc dLoc = TSMimeHdrFieldNextDup(_msg.msgBuffer(), _msg.loc(), _loc);
  return dLoc != TS_NULL_MLOC ? MimeField(_msg, dLoc) : MimeField();
}

inline MimeField
MimeField::lastDup(MsgBase msg, std::string_view name)
{
  assert(msg.hasMsg());

  MimeField f(msg, name);

  if (f.valid()) {
    MimeField fd = f.nextDup();
    while (fd.valid()) {
      f  = std::move(fd);
      fd = f.nextDup();
    }
  }
  return f;
}

inline std::string_view
MimeField::nameGet() const
{
  assert(valid());

  int length;
  const char *s = TSMimeHdrFieldNameGet(_msg.msgBuffer(), _msg.loc(), _loc, &length);

  return std::string_view(s, length);
}

inline void
MimeField::nameSet(std::string_view new_name)
{
  assert(valid());

  assert(new_name.data() && new_name.size());

  TSAssert(TSMimeHdrFieldNameSet(_msg.msgBuffer(), _msg.loc(), _loc, new_name.data(), new_name.length()) == TS_SUCCESS);
}

inline std::string_view
MimeField::valuesGet() const
{
  assert(valid());

  int length;
  const char *s = TSMimeHdrFieldValueStringGet(_msg.msgBuffer(), _msg.loc(), _loc, -1, &length);

  return std::string_view(s, length);
}

inline void
MimeField::valuesSet(std::string_view new_values)
{
  assert(valid());

  if (!new_values.data()) {
    assert(!new_values.size());

    TSAssert(TSMimeHdrFieldValuesClear(_msg.msgBuffer(), _msg.loc(), _loc) == TS_SUCCESS);

  } else {
    TSAssert(TSMimeHdrFieldValueStringSet(_msg.msgBuffer(), _msg.loc(), _loc, -1, new_values.data(), new_values.length()) ==
             TS_SUCCESS);
  }
}

inline int
MimeField::valuesCount() const
{
  assert(valid());

  return TSMimeHdrFieldValuesCount(_msg.msgBuffer(), _msg.loc(), _loc);
}

inline std::string_view
MimeField::valGet(int idx) const
{
  assert(valid());

  assert((idx >= 0) && (idx < valuesCount()));

  int length;
  const char *s = TSMimeHdrFieldValueStringGet(_msg.msgBuffer(), _msg.loc(), _loc, idx, &length);

  return std::string_view(s, length);
}

inline void
MimeField::valSet(int idx, std::string_view new_value)
{
  assert(valid());

  assert((idx >= 0) && (idx < valuesCount()));

  assert((new_value.data() && new_value.size()));

  if (!new_value.data()) {
    assert(!new_value.size());

    TSAssert(TSMimeHdrFieldValueDelete(_msg.msgBuffer(), _msg.loc(), _loc, idx) == TS_SUCCESS);

  } else {
    TSAssert(TSMimeHdrFieldValueStringSet(_msg.msgBuffer(), _msg.loc(), _loc, idx, new_value.data(), new_value.length()) ==
             TS_SUCCESS);
  }
}

inline void
MimeField::valInsert(int idx, std::string_view new_value)
{
  assert(valid());

  assert((new_value.data() && new_value.size()));

  assert((idx >= 0) && (idx < valuesCount()));

  TSAssert(TSMimeHdrFieldValueStringInsert(_msg.msgBuffer(), _msg.loc(), _loc, idx, new_value.data(), new_value.length()) ==
           TS_SUCCESS);
}

inline void
MimeField::valAppend(std::string_view new_value)
{
  assert(valid());

  assert(new_value.data() && new_value.size());

  TSAssert(TSMimeHdrFieldValueStringInsert(_msg.msgBuffer(), _msg.loc(), _loc, -1, new_value.data(), new_value.length()) ==
           TS_SUCCESS);
}

MimeField::~MimeField()
{
  if (valid()) {
    TSAssert(TSHandleMLocRelease(_msg.msgBuffer(), _msg.loc(), _loc) == TS_SUCCESS);
  }
}

} // end namespace atscppapi
