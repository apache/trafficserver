/*
  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this f
  with the License.  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#include "cripts/Lulu.hpp"
#include "cripts/Preamble.hpp"

// Constants
const cripts::Header::Iterator cripts::Header::Iterator::_end =
  cripts::Header::Iterator("__END__", cripts::Header::Iterator::END_TAG);

namespace cripts
{

namespace Method
{
#undef DELETE // ToDo: macOS shenanigans here, defining DELETE as a macro
  const cripts::Header::Method GET(TS_HTTP_METHOD_GET, TS_HTTP_LEN_GET);
  const cripts::Header::Method HEAD(TS_HTTP_METHOD_HEAD, TS_HTTP_LEN_HEAD);
  const cripts::Header::Method POST(TS_HTTP_METHOD_POST, TS_HTTP_LEN_POST);
  const cripts::Header::Method PUT(TS_HTTP_METHOD_PUT, TS_HTTP_LEN_PUT);
  const cripts::Header::Method PUSH(TS_HTTP_METHOD_PUSH, TS_HTTP_LEN_PUSH);
  const cripts::Header::Method DELETE(TS_HTTP_METHOD_DELETE, TS_HTTP_LEN_DELETE);
  const cripts::Header::Method OPTIONS(TS_HTTP_METHOD_OPTIONS, TS_HTTP_LEN_OPTIONS);
  const cripts::Header::Method CONNECT(TS_HTTP_METHOD_CONNECT, TS_HTTP_LEN_CONNECT);
  const cripts::Header::Method TRACE(TS_HTTP_METHOD_TRACE, TS_HTTP_LEN_TRACE);
  // This is a special feature of ATS
  const cripts::Header::Method PURGE(TS_HTTP_METHOD_PURGE, TS_HTTP_LEN_PURGE);
} // namespace Method

Header::Status &
Header::Status::operator=(int status)
{
  _ensure_initialized(_owner);
  _status = static_cast<TSHttpStatus>(status);

  switch (_owner->_state->hook) {
  case TS_HTTP_SEND_RESPONSE_HDR_HOOK:
  case TS_HTTP_READ_RESPONSE_HDR_HOOK:
  case TS_HTTP_TXN_CLOSE_HOOK:
    TSHttpHdrStatusSet(_owner->_bufp, _owner->_hdr_loc, _status);
    break;
  default:
    TSHttpTxnStatusSet(_owner->_state->txnp, _status);
    break;
  }

  _owner->_state->context->p_instance.debug("Setting status = {}", status);

  return *this;
}

Header::Status::operator integer()
{
  if (_status == TS_HTTP_STATUS_NONE) {
    _ensure_initialized(_owner);
    _status = TSHttpHdrStatusGet(_owner->_bufp, _owner->_hdr_loc);
  }
  return _status;
}

Header::Reason &
Header::Reason::operator=(cripts::string_view reason)
{
  _ensure_initialized(_owner);
  TSHttpHdrReasonSet(_owner->_bufp, _owner->_hdr_loc, reason.data(), reason.size());
  _owner->_state->context->p_instance.debug("Setting reason = {}", reason);

  return *this;
}

Header::Body &
Header::Body::operator=(cripts::string_view body)
{
  auto b = static_cast<char *>(TSmalloc(body.size() + 1));

  _ensure_initialized(_owner);
  memcpy(b, body.data(), body.size());
  b[body.size()] = '\0';
  TSHttpTxnErrorBodySet(_owner->_state->txnp, b, body.size(), nullptr);

  return *this;
}

cripts::string_view
Header::Method::GetSV()
{
  if (_method.size() == 0) {
    _ensure_initialized(_owner);
    int         len;
    const char *value = TSHttpHdrMethodGet(_owner->_bufp, _owner->_hdr_loc, &len);

    _method = cripts::string_view(value, static_cast<cripts::string_view::size_type>(len));
  }

  return _method;
}

cripts::string_view
Header::CacheStatus::GetSV()
{
  static std::array<cripts::string_view, 4> names{
    "miss",      // TS_CACHE_LOOKUP_MISS,
    "hit-stale", // TS_CACHE_LOOKUP_HIT_STALE,
    "hit-fresh", // TS_CACHE_LOOKUP_HIT_FRESH,
    "skipped"    // TS_CACHE_LOOKUP_SKIPPED
  };
  int status;

  _ensure_initialized(_owner);
  if (_cache.size() == 0) {
    TSAssert(_owner->_state->txnp);
    if (TSHttpTxnCacheLookupStatusGet(_owner->_state->txnp, &status) == TS_ERROR || status < 0 || status >= 4) {
      _cache = "none";
    } else {
      _cache = names[status];
    }
  }

  return _cache;
}

Header::String &
Header::String::operator=(const cripts::string_view str)
{
  _ensure_initialized(_owner);
  if (_field_loc) {
    if (str.empty()) {
      TSMLoc tmp;

      // Nuke the existing header, this will nuke all of them
      _owner->_state->context->p_instance.debug("Deleting header = {}", _name);
      while (_field_loc) {
        tmp = TSMimeHdrFieldNextDup(_owner->_bufp, _owner->_hdr_loc, _field_loc);
        TSMimeHdrFieldDestroy(_owner->_bufp, _owner->_hdr_loc, _field_loc);
        TSHandleMLocRelease(_owner->_bufp, _owner->_hdr_loc, _field_loc);
        _field_loc = tmp;
      }
    } else {
      TSMLoc tmp   = nullptr;
      bool   first = true;

      // Replace the existing header
      while (_field_loc) {
        tmp = TSMimeHdrFieldNextDup(_owner->_bufp, _owner->_hdr_loc, _field_loc);
        if (first) {
          first = false;
          if (TS_SUCCESS == TSMimeHdrFieldValueStringSet(_owner->_bufp, _owner->_hdr_loc, _field_loc, -1, str.data(), str.size())) {
            _owner->_state->context->p_instance.debug("Replacing header {} = {}", _name, str);
          }
        } else {
          TSMimeHdrFieldDestroy(_owner->_bufp, _owner->_hdr_loc, _field_loc);
        }
        TSHandleMLocRelease(_owner->_bufp, _owner->_hdr_loc, _field_loc);
        _field_loc = tmp;
      }
    }
  } else {
    if (str.size() > 0) {
      // Create a new header
      _owner->_state->context->p_instance.debug("Adding header {} = {} ", _name, str);

      if (TS_SUCCESS == TSMimeHdrFieldCreateNamed(_owner->_bufp, _owner->_hdr_loc, _name.data(), _name.size(), &_field_loc)) {
        if (TS_SUCCESS == TSMimeHdrFieldValueStringSet(_owner->_bufp, _owner->_hdr_loc, _field_loc, -1, str.data(), str.size())) {
          TSMimeHdrFieldAppend(_owner->_bufp, _owner->_hdr_loc, _field_loc);
        }
      }
    } else {
      // We don't allow setting an empty header, no-op
    }
  }

  return *this;
}

Header::String &
Header::String::operator=(integer val)
{
  _ensure_initialized(_owner);
  if (_field_loc) {
    TSMLoc tmp   = nullptr;
    bool   first = true;

    // Replace the existing header
    while (_field_loc) {
      tmp = TSMimeHdrFieldNextDup(_owner->_bufp, _owner->_hdr_loc, _field_loc);
      if (first) {
        first = false;
        // tsapi TSReturnCode TSMimeHdrFieldValueInt64Set(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx, int64_t value);
        if (TS_SUCCESS == TSMimeHdrFieldValueInt64Set(_owner->_bufp, _owner->_hdr_loc, _field_loc, -1, val)) {
          _owner->_state->context->p_instance.debug("Replacing integer header {} = {} ", _name, val);
        }
      } else {
        TSMimeHdrFieldDestroy(_owner->_bufp, _owner->_hdr_loc, _field_loc);
      }
      TSHandleMLocRelease(_owner->_bufp, _owner->_hdr_loc, _field_loc);
      _field_loc = tmp;
    }
  } else {
    // Create a new header
    _owner->_state->context->p_instance.debug("Adding integer header {} = {} ", _name, val);

    if (TS_SUCCESS == TSMimeHdrFieldCreateNamed(_owner->_bufp, _owner->_hdr_loc, _name.data(), _name.size(), &_field_loc)) {
      if (TS_SUCCESS == TSMimeHdrFieldValueInt64Set(_owner->_bufp, _owner->_hdr_loc, _field_loc, -1, val)) {
        TSMimeHdrFieldAppend(_owner->_bufp, _owner->_hdr_loc, _field_loc);
      }
    }
  }
  return *this;
}

Header::String &
Header::String::operator+=(const cripts::string_view str)
{
  _ensure_initialized(_owner);
  if (_field_loc) {
    if (!str.empty()) {
      // Drop the old field loc for now ... ToDo: Oh well, we need to figure out how to handle multi-value headers better
      TSHandleMLocRelease(_owner->_bufp, _owner->_hdr_loc, _field_loc);

      if (TS_SUCCESS == TSMimeHdrFieldCreateNamed(_owner->_bufp, _owner->_hdr_loc, _name.data(), _name.size(), &_field_loc)) {
        if (TS_SUCCESS == TSMimeHdrFieldValueStringSet(_owner->_bufp, _owner->_hdr_loc, _field_loc, -1, str.data(), str.size())) {
          _owner->_state->context->p_instance.debug("Appending header {} = {} ", _name, str);

          TSMimeHdrFieldAppend(_owner->_bufp, _owner->_hdr_loc, _field_loc);
        }
      } // Nothing to append with an empty header, we don't allow that
    }
  } else {
    // No previous field, so just make a new one
    operator=(str);
  }

  return *this;
}

Header::String
Header::operator[](const cripts::string_view str)
{
  _ensure_initialized(this);
  TSAssert(_bufp && _hdr_loc);

  TSMLoc         field_loc = TSMimeHdrFieldFind(_bufp, _hdr_loc, str.data(), str.size());
  Header::String ret;

  if (field_loc) {
    int         len   = 0;
    const char *value = TSMimeHdrFieldValueStringGet(_bufp, _hdr_loc, field_loc, -1, &len);

    ret._initialize(str, cripts::string_view(value, len), this, field_loc);
  } else {
    ret._initialize(str, {}, this, nullptr);
  }

  return ret;
}

Client::Request &
Client::Request::_get(cripts::Context *context)
{
  _ensure_initialized(&context->_client.request);
  return context->_client.request;
}

void
Client::Request::_initialize()
{
  TSAssert(_state->txnp);

  if (TSHttpTxnClientReqGet(_state->txnp, &_bufp, &_hdr_loc) != TS_SUCCESS) {
    _state->error.Fail();
  } else {
    super_type::_initialize(); // Don't initialize unless properly setup
  }
}

Header::Iterator
Header::begin()
{
  _ensure_initialized(this);
  // Cleanup any lingering iterator state
  if (_iterator_loc) {
    TSHandleMLocRelease(_bufp, _hdr_loc, _iterator_loc);
    _iterator_loc = nullptr;
  }

  // Start the new iterator
  ++_iterator_tag;
  _iterator_loc = TSMimeHdrFieldGet(_bufp, _hdr_loc, 0);

  if (_iterator_loc) {
    int                 name_len;
    const char         *name = TSMimeHdrFieldNameGet(_bufp, _hdr_loc, _iterator_loc, &name_len);
    cripts::string_view name_view(name, name_len);

    return {name_view, _iterator_tag, this};

  } else {
    return Iterator::end(); // Seems unlikely that we'd not have any headers...
  }
}

cripts::string_view
Header::iterate()
{
  _ensure_initialized(this);
  TSMLoc next_loc = TSMimeHdrFieldNext(_bufp, _hdr_loc, _iterator_loc);

  TSHandleMLocRelease(_bufp, _hdr_loc, _iterator_loc);
  _iterator_loc = next_loc;

  if (_iterator_loc) {
    int         name_len;
    const char *name = TSMimeHdrFieldNameGet(_bufp, _hdr_loc, _iterator_loc, &name_len);

    return {name, static_cast<cripts::string_view::size_type>(name_len)};
  } else {
    return "";
  }
}

Client::Response &
Client::Response::_get(cripts::Context *context)
{
  _ensure_initialized(&context->_client.response);
  return context->_client.response;
}

void
Client::Response::_initialize()
{
  CAssert(_state->hook != TS_HTTP_READ_REQUEST_HDR_HOOK);
  CAssert(_state->hook != TS_HTTP_POST_REMAP_HOOK);
  CAssert(_state->hook != TS_HTTP_SEND_REQUEST_HDR_HOOK);

  TSAssert(_state->txnp);

  if (TSHttpTxnClientRespGet(_state->txnp, &_bufp, &_hdr_loc) != TS_SUCCESS) {
    _state->error.Fail();
  } else {
    super_type::_initialize(); // Don't initialize unless properly setup
  }
}

Server::Request &
Server::Request::_get(cripts::Context *context)
{
  _ensure_initialized(&context->_server.request);
  return context->_server.request;
}

void
Server::Request::_initialize()
{
  CAssert(_state->hook != TS_HTTP_READ_REQUEST_HDR_HOOK);
  CAssert(_state->hook != TS_HTTP_POST_REMAP_HOOK);
  CAssert(_state->hook != TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK);
  CAssert(_state->hook != TS_HTTP_READ_RESPONSE_HDR_HOOK);

  if (!Initialized()) {
    TSAssert(_state->txnp);
    if (TSHttpTxnServerReqGet(_state->txnp, &_bufp, &_hdr_loc) != TS_SUCCESS) {
      _state->error.Fail();
    } else {
      super_type::_initialize(); // Don't initialize unless properly setup
    }
  }
}

Server::Response &
Server::Response::_get(cripts::Context *context)
{
  _ensure_initialized(&context->_server.response);
  return context->_server.response;
}

void
Server::Response::_initialize()
{
  CAssert(_state->hook != TS_HTTP_READ_REQUEST_HDR_HOOK);
  CAssert(_state->hook != TS_HTTP_POST_REMAP_HOOK);
  CAssert(_state->hook != TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK);
  CAssert(_state->hook != TS_HTTP_SEND_REQUEST_HDR_HOOK);

  TSAssert(_state->txnp);

  if (TSHttpTxnServerRespGet(_state->txnp, &_bufp, &_hdr_loc) != TS_SUCCESS) {
    _state->error.Fail();
  } else {
    super_type::_initialize(); // Don't initialize unless properly setup
  }
}

} // namespace cripts
