/*
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
#include <sstream>

#include "cripts/Lulu.hpp"
#include "cripts/Preamble.hpp"

namespace cripts
{

std::vector<cripts::string_view>
Url::Component::Split(const char delim)
{
  return cripts::Splitter(GetSV(), delim);
}

cripts::string_view
Url::Scheme::GetSV()
{
  if (_owner && _data.empty()) {
    const char *value = nullptr;
    int         len   = 0;

    _ensure_initialized(_owner);
    value = TSUrlSchemeGet(_owner->_bufp, _owner->_urlp, &len);
    _data = cripts::string_view(value, len);
  }

  return _data;
}

Url::Scheme
Url::Scheme::operator=(cripts::string_view scheme)
{
  CAssert(!_owner->ReadOnly()); // This can not be a read-only URL
  _ensure_initialized(_owner);
  TSUrlSchemeSet(_owner->_bufp, _owner->_urlp, scheme.data(), scheme.size());
  _owner->_modified = true;
  Reset();
  _loaded = false;

  return *this;
}

cripts::string_view
Url::Host::GetSV()
{
  if (_owner && _data.empty()) {
    const char *value = nullptr;
    int         len   = 0;

    _ensure_initialized(_owner);
    value   = TSUrlHostGet(_owner->_bufp, _owner->_urlp, &len);
    _data   = cripts::string_view(value, len);
    _loaded = true;
  }

  return _data;
}

Url::Host
Url::Host::operator=(cripts::string_view host)
{
  _ensure_initialized(_owner);
  CAssert(!_owner->ReadOnly()); // This can not be a read-only URL
  TSUrlHostSet(_owner->_bufp, _owner->_urlp, host.data(), host.size());
  _owner->_modified = true;
  Reset();
  _loaded = false;

  return *this;
}

Url::Port::operator integer() // This should not be explicit
{
  _ensure_initialized(_owner);
  if (_owner && _port < 0) {
    _port = TSUrlPortGet(_owner->_bufp, _owner->_urlp);
  }

  return _port;
}

Url::Port
Url::Port::operator=(int port)
{
  _ensure_initialized(_owner);
  CAssert(!_owner->ReadOnly()); // This can not be a read-only URL
  TSUrlPortSet(_owner->_bufp, _owner->_urlp, port);
  _owner->_modified = true;
  Reset();

  return *this;
}

cripts::string_view
Url::Path::GetSV()
{
  if (_segments.size() > 0) {
    std::ostringstream path;

    std::copy(_segments.begin(), _segments.end(), std::ostream_iterator<cripts::string_view>(path, "/"));
    _storage.reserve(_size);
    _storage = std::string_view(path.str());
    if (_storage.size() > 0) {
      _storage.pop_back(); // Removes the trailing /
    }

    return {_storage};
  } else if (_owner && _data.empty()) {
    const char *value = nullptr;
    int         len   = 0;

    _ensure_initialized(_owner);
    value   = TSUrlPathGet(_owner->_bufp, _owner->_urlp, &len);
    _data   = cripts::string_view(value, len);
    _size   = len;
    _loaded = true;
  }

  return _data;
}

Url::Path::String
Url::Path::operator[](Segments::size_type ix)
{
  Url::Path::String ret;

  _ensure_initialized(_owner);
  _parser(); // Make sure the segments are loaded
  if (ix < _segments.size()) {
    ret._initialize(_segments[ix], this, ix);
  }

  return ret; // RVO
}

Url::Path
Url::Path::operator=(cripts::string_view path)
{
  _ensure_initialized(_owner);
  CAssert(!_owner->ReadOnly()); // This can not be a read-only URL
  TSUrlPathSet(_owner->_bufp, _owner->_urlp, path.data(), path.size());
  _owner->_modified = true;
  Reset();
  _loaded = false;

  return *this;
}

cripts::string
Url::Path::operator+=(cripts::string_view add)
{
  cripts::string str;

  if (add.size() > 0) {
    str.assign(GetSV());
    str += add;
    operator=(str);
  }

  return str; // RVO
}

Url::Path::String &
Url::Path::String::operator=(const cripts::string_view str)
{
  _ensure_initialized(_owner->_owner);
  CAssert(!_owner->_owner->ReadOnly()); // This can not be a read-only URL
  _owner->_size          -= _owner->_segments[_ix].size();
  _owner->_segments[_ix]  = str;
  _owner->_size          += str.size();
  _owner->_modified       = true;

  return *this;
}

void
Url::Path::Reset()
{
  Component::Reset();

  _segments.clear();
  _storage  = "";
  _size     = 0;
  _modified = false;
}

void
Url::Path::Push(cripts::string_view val)
{
  _parser();
  _modified = true;
  _segments.push_back(val);
}

void
Url::Path::Insert(Segments::size_type ix, cripts::string_view val)
{
  _parser();
  _modified = true;
  _segments.insert(_segments.begin() + ix, val);
}

void
Url::Path::_parser()
{
  if (_segments.size() == 0) {
    _segments = Split('/');
  }
}

Url::Query::Parameter &
Url::Query::Parameter::operator=(const cripts::string_view str)
{
  _ensure_initialized(_owner->_owner);
  CAssert(!_owner->_owner->ReadOnly()); // This can not be a read-only URL
  auto iter = _owner->_hashed.find(_name);

  if (iter != _owner->_hashed.end()) {
    iter->second = str; // Can be an empty string here!
  } else {
    _owner->_ordered.push_back(_name);
    _owner->_hashed[_name] = str;
  }
  _owner->_modified = true;

  return *this;
}

cripts::string_view
Url::Query::GetSV()
{
  _ensure_initialized(_owner);
  if (_ordered.size() > 0) {
    _storage.clear();
    _storage.reserve(_size);

    // ToDo: This is wonky, has to be a better std:: iteration to do here
    for (const auto key : _ordered) {
      auto iter = _hashed.find(key);

      if (_storage.size() > 0) {
        _storage += "&";
      }

      if (iter != _hashed.end()) {
        _storage += iter->first;
        if (iter->second.size() > 0) {
          _storage += '=';
          _storage += iter->second;
        }
      }
    }

    return {_storage};
  }

  // This gets weird when we modify the query parameter components, and can possibly empty
  // the entire query parameter. At which point, we don't want to reload the string_view
  // from the URL object inside of ATS...
  if (_owner && !_loaded) {
    const char *value = nullptr;
    int         len   = 0;

    value   = TSUrlHttpQueryGet(_owner->_bufp, _owner->_urlp, &len);
    _data   = cripts::string_view(value, len);
    _size   = len;
    _loaded = true;
  }

  return _data;
}

Url::Query
Url::Query::operator=(cripts::string_view query)
{
  _ensure_initialized(_owner);
  CAssert(!_owner->ReadOnly()); // This can not be a read-only URL
  TSUrlHttpQuerySet(_owner->_bufp, _owner->_urlp, query.data(), query.size());
  _owner->_modified = true;
  Reset();
  _loaded = false;

  return *this;
}

cripts::string
Url::Query::operator+=(cripts::string_view add)
{
  cripts::string str;

  if (add.size() > 0) {
    str.assign(GetSV());
    str += add;
    operator=(str);
  }

  return str; // RVO
}

Url::Query::Parameter
Url::Query::operator[](cripts::string_view param)
{
  // Make sure the hash and vector are populated
  _ensure_initialized(_owner);
  _parser();

  Parameter ret;
  auto      iter = _hashed.find(param);

  if (iter != _hashed.end()) {
    ret._initialize(iter->first, iter->second, this);
  } else {
    ret._initialize(param, "", this);
  }

  return ret;
}

void
Url::Query::Erase(cripts::string_view param)
{
  // Make sure the hash and vector are populated
  _parser();

  auto iter  = _hashed.find(param);
  auto viter = std::find(_ordered.begin(), _ordered.end(), param);

  if (iter != _hashed.end()) {
    _size -= iter->second.size(); // Size of the erased value
    _hashed.erase(iter);

    CAssert(viter != _ordered.end());
    _size -= viter->size(); // Length of the erased key
    _ordered.erase(viter);

    if (_ordered.size() == 0) {
      Reset();
    }
    _modified = true; // Make sure to set this after we reset above ...
  }
}

void
Url::Query::Erase(std::initializer_list<cripts::string_view> list, bool keep)
{
  if (keep) {
    // Make sure the hash and vector are populated
    _parser();

    for (auto viter = _ordered.begin(); viter != _ordered.end();) {
      if (list.end() == std::find(list.begin(), list.end(), *viter)) {
        auto iter = _hashed.find(*viter);

        CAssert(iter != _hashed.end());
        _size -= iter->second.size(); // Size of the erased value
        _size -= viter->size();       // Length of the erased key
        _hashed.erase(iter);
        viter     = _ordered.erase(viter);
        _modified = true;
      } else {
        ++viter;
      }
    }
    if (_ordered.size() == 0) {
      Reset();
    }
  } else {
    for (auto &it : list) {
      Erase(it);
    }
  }
}

void
Url::Query::Reset()
{
  Component::Reset();

  _ordered.clear();
  _hashed.clear();
  _storage  = "";
  _size     = 0;
  _modified = false;
}

void
Url::Query::_parser()
{
  if (_ordered.size() == 0) {
    for (const auto sv : Split('&')) {
      const auto          eq  = sv.find_first_of('=');
      cripts::string_view key = sv.substr(0, eq);
      cripts::string_view val;

      if (eq != cripts::string_view::npos) {
        val = sv.substr(eq + 1);
      }

      _ordered.push_back(key); // Keep the order
      _hashed[key] = val;
    }
  }
}

cripts::string
Url::String()
{
  cripts::string ret;

  CAssert(_context);
  _ensure_initialized(this);
  int   full_len = 0;
  char *full_str = TSUrlStringGet(_bufp, _urlp, &full_len);

  if (full_str) {
    ret.assign(full_str, full_len);
    TSfree(static_cast<void *>(full_str));
  }

  return ret;
}

Pristine::URL &
Pristine::URL::_get(cripts::Context *context)
{
  _ensure_initialized(&context->_urls.pristine);
  return context->_urls.pristine;
}

void
Pristine::URL::_initialize(cripts::Context *context)
{
  Pristine::URL *url = &context->_urls.pristine;

  TSAssert(context->state.txnp);
  if (TSHttpTxnPristineUrlGet(context->state.txnp, &url->_bufp, &url->_urlp) != TS_SUCCESS) {
    context->state.error.Fail();
  } else {
    super_type::_initialize(context); // Only if successful
  }
}

void
Client::URL::_initialize(cripts::Context *context)
{
  if (context->rri) {
    _bufp    = context->rri->requestBufp;
    _hdr_loc = context->rri->requestHdrp;
    _urlp    = context->rri->requestUrl;
    super_type::_initialize(context);
  } else {
    Client::Request &req = Client::Request::_get(context); // Repurpose / create the shared request object

    _bufp    = req.BufP();
    _hdr_loc = req.MLoc();

    if (TSHttpHdrUrlGet(_bufp, _hdr_loc, &_urlp) != TS_SUCCESS) {
      context->state.error.Fail();
    } else {
      super_type::_initialize(context);
    }
  }
}

Client::URL &
Client::URL::_get(cripts::Context *context)
{
  _ensure_initialized(&context->_urls.request);
  return context->_urls.request;
}

bool
Client::URL::_update()
{
  _ensure_initialized(this);
  path.Flush();
  query.Flush();

  return _modified;
}

void
Remap::From::URL::_initialize(cripts::Context *context)
{
  super_type::_initialize(context);
  _bufp    = context->rri->requestBufp;
  _hdr_loc = context->rri->requestHdrp;
  _urlp    = context->rri->mapFromUrl;
}

Remap::From::URL &
Remap::From::URL::_get(cripts::Context *context)
{
  _ensure_initialized(&context->_urls.remap.from);
  return context->_urls.remap.from;
}

void
Remap::To::URL::_initialize(cripts::Context *context)
{
  super_type::_initialize(context);

  _bufp    = context->rri->requestBufp;
  _hdr_loc = context->rri->requestHdrp;
  _urlp    = context->rri->mapToUrl;
}

Remap::To::URL &
Remap::To::URL::_get(cripts::Context *context)
{
  _ensure_initialized(&context->_urls.remap.to);
  return context->_urls.remap.to;
}

Cache::URL &
Cache::URL::_get(cripts::Context *context)
{
  _ensure_initialized(&context->_urls.cache);
  return context->_urls.cache;
}

void
Cache::URL::_initialize(cripts::Context *context)
{
  Cache::URL      *url = &context->_urls.cache;
  Client::Request &req = Client::Request::_get(context); // Repurpose / create the shared request object

  switch (context->state.hook) {
  // In these hooks, the internal cache-url has been properly set
  case TS_HTTP_SEND_RESPONSE_HDR_HOOK:
  case TS_HTTP_READ_RESPONSE_HDR_HOOK:
  case TS_HTTP_SEND_REQUEST_HDR_HOOK:
  case TS_HTTP_TXN_CLOSE_HOOK:
    if (TSUrlCreate(req.BufP(), &url->_urlp) == TS_SUCCESS) {
      TSAssert(context->state.txnp);
      if (TSHttpTxnCacheLookupUrlGet(context->state.txnp, req.BufP(), url->_urlp) != TS_SUCCESS) {
        context->state.error.Fail();
        return;
      }
    } else {
      context->state.error.Fail();
      return;
    }
    break;
  default: { // This means we have to clone. ToDo: For now, this is implicitly using Client::URL
    Client::URL &src = Client::URL::_get(context);

    if (TSUrlClone(req.BufP(), req.BufP(), src.UrlP(), &url->_urlp) != TS_SUCCESS) {
      context->state.error.Fail();
      return;
    }
  } break;
  }

  // Only if we succeeded above
  super_type::_initialize(context);
  _bufp    = req.BufP();
  _hdr_loc = req.MLoc();
}

// This has to be implemented here, since the cripts::Context is not complete yet
bool
Cache::URL::_update()
{
  // For correctness, we will also make sure the Path and Query objects are flushed
  path.Flush();
  query.Flush();

  if (_modified) {
    _ensure_initialized(&_context->_urls.cache);
    TSAssert(_context->state.txnp);
    _modified = false;
    if (TS_SUCCESS == TSHttpTxnCacheLookupUrlSet(_context->state.txnp, _bufp, _urlp)) {
      if (_context->p_instance.DebugOn()) {
        _context->p_instance.debug("Successfully setting cache-key to {}", String());
      }
      return true;
    } else {
      if (_context->p_instance.DebugOn()) {
        _context->p_instance.debug("Could not set the cache key to {}", String());
      }
      _context->state.error.Fail();
      return false;
    }
  }

  return false;
}

// ToDo: This may need more work, to understand which hooks the parent URL is actually available
Parent::URL &
Parent::URL::_get(cripts::Context *context)
{
  _ensure_initialized(&context->_urls.parent);
  return context->_urls.parent;
}

void
Parent::URL::_initialize(cripts::Context *context)
{
  Parent::URL     *url = &context->_urls.parent;
  Client::Request &req = Client::Request::_get(context); // Repurpose / create the shared request object

  if (TSUrlCreate(req.BufP(), &url->_urlp) == TS_SUCCESS) {
    TSAssert(context->state.txnp);
    if (TSHttpTxnParentSelectionUrlGet(context->state.txnp, req.BufP(), url->_urlp) != TS_SUCCESS) {
      context->state.error.Fail();
      return;
    }
  } else {
    context->state.error.Fail();
    return;
  }

  // Only if successful above
  super_type::_initialize(context);
  _bufp    = req.BufP();
  _hdr_loc = req.MLoc();
}

} // namespace cripts
