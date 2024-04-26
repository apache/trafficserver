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
const Header::Iterator Header::Iterator::_end = Header::Iterator("__END__", Header::Iterator::END_TAG);

namespace Cript::Method
{
#undef DELETE // ToDo: macOS shenanigans here, defining DELETE as a macro
const Header::Method GET(TS_HTTP_METHOD_GET, TS_HTTP_LEN_GET);
const Header::Method HEAD(TS_HTTP_METHOD_HEAD, TS_HTTP_LEN_HEAD);
const Header::Method POST(TS_HTTP_METHOD_POST, TS_HTTP_LEN_POST);
const Header::Method PUT(TS_HTTP_METHOD_PUT, TS_HTTP_LEN_PUT);
const Header::Method PUSH(TS_HTTP_METHOD_PUSH, TS_HTTP_LEN_PUSH);
const Header::Method DELETE(TS_HTTP_METHOD_DELETE, TS_HTTP_LEN_DELETE);
const Header::Method OPTIONS(TS_HTTP_METHOD_OPTIONS, TS_HTTP_LEN_OPTIONS);
const Header::Method CONNECT(TS_HTTP_METHOD_CONNECT, TS_HTTP_LEN_CONNECT);
const Header::Method TRACE(TS_HTTP_METHOD_TRACE, TS_HTTP_LEN_TRACE);
// This is a special feature of ATS
const Header::Method PURGE(TS_HTTP_METHOD_PURGE, TS_HTTP_LEN_PURGE);
} // namespace Cript::Method

Header::Status &
Header::Status::operator=(int status)
{
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
    _status = TSHttpHdrStatusGet(_owner->_bufp, _owner->_hdr_loc);
  }
  return _status;
}

Header::Reason &
Header::Reason::operator=(Cript::string_view reason)
{
  TSHttpHdrReasonSet(_owner->_bufp, _owner->_hdr_loc, reason.data(), reason.size());
  _owner->_state->context->p_instance.debug("Setting reason = {}", reason);

  return *this;
}

Header::Body &
Header::Body::operator=(Cript::string_view body)
{
  auto b = static_cast<char *>(TSmalloc(body.size() + 1));

  memcpy(b, body.data(), body.size());
  b[body.size()] = '\0';
  TSHttpTxnErrorBodySet(_owner->_state->txnp, b, body.size(), nullptr);

  return *this;
}

Cript::string_view
Header::Method::getSV()
{
  if (_method.size() == 0) {
    int         len;
    const char *value = TSHttpHdrMethodGet(_owner->_bufp, _owner->_hdr_loc, &len);

    _method = Cript::string_view(value, static_cast<Cript::string_view::size_type>(len));
  }

  return _method;
}

Cript::string_view
Header::CacheStatus::getSV()
{
  static std::array<Cript::string_view, 4> names{
    "miss",      // TS_CACHE_LOOKUP_MISS,
    "hit-stale", // TS_CACHE_LOOKUP_HIT_STALE,
    "hit-fresh", // TS_CACHE_LOOKUP_HIT_FRESH,
    "skipped"    // TS_CACHE_LOOKUP_SKIPPED
  };
  int status;

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
Header::String::operator=(const Cript::string_view str)
{
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
Header::String::operator+=(const Cript::string_view str)
{
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
Header::operator[](const Cript::string_view str)
{
  TSAssert(_bufp && _hdr_loc);

  TSMLoc         field_loc = TSMimeHdrFieldFind(_bufp, _hdr_loc, str.data(), str.size());
  Header::String ret;

  if (field_loc) {
    int         len   = 0;
    const char *value = TSMimeHdrFieldValueStringGet(_bufp, _hdr_loc, field_loc, -1, &len);

    ret.initialize(str, Cript::string_view(value, len), this, field_loc);
  } else {
    ret.initialize(str, {}, this, nullptr);
  }

  return ret;
}

Client::Request &
Client::Request::_get(Cript::Context *context)
{
  Client::Request *request = &context->_client_req_header;

  if (!request->initialized()) {
    TSAssert(context->state.txnp);
    if (TSHttpTxnClientReqGet(context->state.txnp, &request->_bufp, &request->_hdr_loc) != TS_SUCCESS) {
      context->state.error.fail();
    } else {
      request->initialize(&context->state); // Don't initialize unless properly setup
    }
  }

  return context->_client_req_header;
}

Header::Iterator
Header::begin()
{
  // Cleanup any lingering iterator state
  if (_iterator_loc) {
    TSHandleMLocRelease(_bufp, _hdr_loc, _iterator_loc);
    _iterator_loc = nullptr;
  }

  // Start the new iterator
  ++_iterator_tag;
  _iterator_loc = TSMimeHdrFieldGet(_bufp, _hdr_loc, 0);

  if (_iterator_loc) {
    int                name_len;
    const char        *name = TSMimeHdrFieldNameGet(_bufp, _hdr_loc, _iterator_loc, &name_len);
    Cript::string_view name_view(name, name_len);

    return {name_view, _iterator_tag, this};

  } else {
    return Iterator::end(); // Seems unlikely that we'd not have any headers...
  }
}

Cript::string_view
Header::iterate()
{
  TSMLoc next_loc = TSMimeHdrFieldNext(_bufp, _hdr_loc, _iterator_loc);

  TSHandleMLocRelease(_bufp, _hdr_loc, _iterator_loc);
  _iterator_loc = next_loc;

  if (_iterator_loc) {
    int         name_len;
    const char *name = TSMimeHdrFieldNameGet(_bufp, _hdr_loc, _iterator_loc, &name_len);

    return {name, static_cast<Cript::string_view::size_type>(name_len)};
  } else {
    return "";
  }
}

Client::Response &
Client::Response::_get(Cript::Context *context)
{
  Client::Response *response = &context->_client_resp_header;

  if (!response->initialized()) {
    TSAssert(context->state.txnp);
    if (TSHttpTxnClientRespGet(context->state.txnp, &response->_bufp, &response->_hdr_loc) != TS_SUCCESS) {
      context->state.error.fail();
    } else {
      response->initialize(&context->state); // Don't initialize unless properly setup
    }
  }

  return context->_client_resp_header;
}

Server::Request &
Server::Request::_get(Cript::Context *context)
{
  Server::Request *request = &context->_server_req_header;

  if (!request->initialized()) {
    TSAssert(context->state.txnp);
    if (TSHttpTxnServerReqGet(context->state.txnp, &request->_bufp, &request->_hdr_loc) != TS_SUCCESS) {
      context->state.error.fail();
    } else {
      request->initialize(&context->state); // Don't initialize unless properly setup
    }
  }

  return context->_server_req_header;
}

Server::Response &
Server::Response::_get(Cript::Context *context)
{
  Server::Response *response = &context->_server_resp_header;

  if (!response->initialized()) {
    TSAssert(context->state.txnp);
    if (TSHttpTxnServerRespGet(context->state.txnp, &response->_bufp, &response->_hdr_loc) != TS_SUCCESS) {
      context->state.error.fail();
    } else {
      response->initialize(&context->state); // Don't initialize unless properly setup
    }
  }

  return context->_server_resp_header;
}
