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

#include "cripts/Lulu.hpp"
#include "cripts/Preamble.hpp"

std::vector<Cript::string_view>
Cript::Url::Component::split(const char delim)
{
  return Cript::splitter(getSV(), delim);
}

Cript::string_view
Cript::Url::Scheme::getSV()
{
  if (_owner && _data.empty()) {
    const char *value = nullptr;
    int         len   = 0;

    value = TSUrlSchemeGet(_owner->_bufp, _owner->_urlp, &len);
    _data = Cript::string_view(value, len);
  }

  return _data;
}

Cript::Url::Scheme
Cript::Url::Scheme::operator=(Cript::string_view scheme)
{
  TSUrlSchemeSet(_owner->_bufp, _owner->_urlp, scheme.data(), scheme.size());
  _owner->_modified = true;
  reset();
  _loaded = false;

  return *this;
}

Cript::string_view
Cript::Url::Host::getSV()
{
  if (_owner && _data.empty()) {
    const char *value = nullptr;
    int         len   = 0;

    value   = TSUrlHostGet(_owner->_bufp, _owner->_urlp, &len);
    _data   = Cript::string_view(value, len);
    _loaded = true;
  }

  return _data;
}

Cript::Url::Host
Cript::Url::Host::operator=(Cript::string_view host)
{
  TSUrlHostSet(_owner->_bufp, _owner->_urlp, host.data(), host.size());
  _owner->_modified = true;
  reset();
  _loaded = false;

  return *this;
}

Cript::Url::Port::operator integer() // This should not be explicit
{
  if (_owner && _port < 0) {
    _port = TSUrlPortGet(_owner->_bufp, _owner->_urlp);
  }

  return _port;
}

Cript::Url::Port
Cript::Url::Port::operator=(int port)
{
  TSUrlPortSet(_owner->_bufp, _owner->_urlp, port);
  _owner->_modified = true;
  reset();

  return *this;
}

Cript::string_view
Cript::Url::Path::getSV()
{
  if (_segments.size() > 0) {
    std::ostringstream path;

    std::copy(_segments.begin(), _segments.end(), std::ostream_iterator<Cript::string_view>(path, "/"));
    _storage.reserve(_size);
    _storage = std::string_view(path.str());
    if (_storage.size() > 0) {
      _storage.pop_back(); // Removes the trailing /
    }

    return {_storage};
  } else if (_owner && _data.empty()) {
    const char *value = nullptr;
    int         len   = 0;

    value   = TSUrlPathGet(_owner->_bufp, _owner->_urlp, &len);
    _data   = Cript::string_view(value, len);
    _size   = len;
    _loaded = true;
  }

  return _data;
}

Cript::Url::Path::String
Cript::Url::Path::operator[](Segments::size_type ix)
{
  Url::Path::String ret;

  _parser(); // Make sure the segments are loaded
  if (ix < _segments.size()) {
    ret._initialize(_segments[ix], this, ix);
  }

  return ret; // RVO
}

Cript::Url::Path
Cript::Url::Path::operator=(Cript::string_view path)
{
  TSUrlPathSet(_owner->_bufp, _owner->_urlp, path.data(), path.size());
  _owner->_modified = true;
  reset();
  _loaded = false;

  return *this;
}

Cript::string
Cript::Url::Path::operator+=(Cript::string_view add)
{
  Cript::string str;

  if (add.size() > 0) {
    str.assign(getSV());
    str += add;
    operator=(str);
  }

  return str; // RVO
}

Cript::Url::Path::String &
Cript::Url::Path::String::operator=(const Cript::string_view str)
{
  _owner->_size          -= _owner->_segments[_ix].size();
  _owner->_segments[_ix]  = str;
  _owner->_size          += str.size();
  _owner->_modified       = true;

  return *this;
}

void
Cript::Url::Path::reset()
{
  Component::reset();

  _segments.clear();
  _storage  = "";
  _size     = 0;
  _modified = false;
}

void
Cript::Url::Path::push(Cript::string_view val)
{
  _parser();
  _modified = true;
  _segments.push_back(val);
}

void
Cript::Url::Path::insert(Segments::size_type ix, Cript::string_view val)
{
  _parser();
  _modified = true;
  _segments.insert(_segments.begin() + ix, val);
}

void
Cript::Url::Path::_parser()
{
  if (_segments.size() == 0) {
    _segments = split('/');
  }
}

Cript::Url::Query::Parameter &
Cript::Url::Query::Parameter::operator=(const Cript::string_view str)
{
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

Cript::string_view
Cript::Url::Query::getSV()
{
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
    _data   = Cript::string_view(value, len);
    _size   = len;
    _loaded = true;
  }

  return _data;
}

Cript::Url::Query
Cript::Url::Query::operator=(Cript::string_view query)
{
  TSUrlHttpQuerySet(_owner->_bufp, _owner->_urlp, query.data(), query.size());
  _owner->_modified = true;
  reset();
  _loaded = false;

  return *this;
}

Cript::string
Cript::Url::Query::operator+=(Cript::string_view add)
{
  Cript::string str;

  if (add.size() > 0) {
    str.assign(getSV());
    str += add;
    operator=(str);
  }

  return str; // RVO
}

Cript::Url::Query::Parameter
Cript::Url::Query::operator[](Cript::string_view param)
{
  // Make sure the hash and vector are populated
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
Cript::Url::Query::erase(Cript::string_view param)
{
  // Make sure the hash and vector are populated
  _parser();

  auto iter  = _hashed.find(param);
  auto viter = std::find(_ordered.begin(), _ordered.end(), param);

  if (iter != _hashed.end()) {
    _size -= iter->second.size(); // Size of the erased value
    _hashed.erase(iter);

    TSReleaseAssert(viter != _ordered.end());
    _size -= viter->size(); // Length of the erased key
    _ordered.erase(viter);

    if (_ordered.size() == 0) {
      reset();
    }
    _modified = true; // Make sure to set this after we reset above ...
  }
}

void
Cript::Url::Query::erase(std::initializer_list<Cript::string_view> list, bool keep)
{
  if (keep) {
    // Make sure the hash and vector are populated
    _parser();

    for (auto viter = _ordered.begin(); viter != _ordered.end();) {
      if (list.end() == std::find(list.begin(), list.end(), *viter)) {
        auto iter = _hashed.find(*viter);

        TSReleaseAssert(iter != _hashed.end());
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
      reset();
    }
  } else {
    for (auto &it : list) {
      erase(it);
    }
  }
}

void
Cript::Url::Query::reset()
{
  Component::reset();

  _ordered.clear();
  _hashed.clear();
  _storage  = "";
  _size     = 0;
  _modified = false;
}

void
Cript::Url::Query::_parser()
{
  if (_ordered.size() == 0) {
    for (const auto sv : split('&')) {
      const auto         eq  = sv.find_first_of('=');
      Cript::string_view key = sv.substr(0, eq);
      Cript::string_view val;

      if (eq != Cript::string_view::npos) {
        val = sv.substr(eq + 1);
      }

      _ordered.push_back(key); // Keep the order
      _hashed[key] = val;
    }
  }
}

Cript::string
Cript::Url::url() const
{
  Cript::string ret;

  if (_state) {
    int   full_len = 0;
    char *full_str = TSUrlStringGet(_bufp, _urlp, &full_len);

    if (full_str) {
      ret.assign(full_str, full_len);
      TSfree(static_cast<void *>(full_str));
    }
  }

  return ret;
}

Pristine::URL &
Pristine::URL::_get(Cript::Context *context)
{
  if (!context->_pristine_url.initialized()) {
    Pristine::URL *url = &context->_pristine_url;

    TSAssert(context->state.txnp);
    if (TSHttpTxnPristineUrlGet(context->state.txnp, &url->_bufp, &url->_urlp) != TS_SUCCESS) {
      context->state.error.fail();
    } else {
      url->_initialize(&context->state);
    }
  }

  return context->_pristine_url;
}

void
Client::URL::_initialize(Cript::Context *context)
{
  Url::_initialize(&context->state);

  _bufp    = context->rri->requestBufp;
  _hdr_loc = context->rri->requestHdrp;
  _urlp    = context->rri->requestUrl;
}

Client::URL &
Client::URL::_get(Cript::Context *context)
{
  if (!context->_client_url.initialized()) {
    context->_client_url._initialize(context);
  }

  return context->_client_url;
}

bool
Client::URL::_update(Cript::Context *context)
{
  path.flush();
  query.flush();

  return _modified;
}

void
Remap::From::URL::_initialize(Cript::Context *context)
{
  Url::_initialize(&context->state);

  _bufp    = context->rri->requestBufp;
  _hdr_loc = context->rri->requestHdrp;
  _urlp    = context->rri->mapFromUrl;
}

Remap::From::URL &
Remap::From::URL::_get(Cript::Context *context)
{
  if (!context->_remap_from_url.initialized()) {
    context->_remap_from_url._initialize(context);
  }

  return context->_remap_from_url;
}

void
Remap::To::URL::_initialize(Cript::Context *context)
{
  Url::_initialize(&context->state);

  _bufp    = context->rri->requestBufp;
  _hdr_loc = context->rri->requestHdrp;
  _urlp    = context->rri->mapToUrl;
}

Remap::To::URL &
Remap::To::URL::_get(Cript::Context *context)
{
  if (!context->_remap_to_url.initialized()) {
    context->_remap_to_url._initialize(context);
  }

  return context->_remap_to_url;
}

Cache::URL &
Cache::URL::_get(Cript::Context *context)
{
  if (!context->_cache_url.initialized()) {
    Cache::URL      *url = &context->_cache_url;
    Client::Request &req = Client::Request::_get(context); // Repurpose / create the shared request object

    switch (context->state.hook) {
    // In these hooks, the internal cache-url has been properly set
    case TS_HTTP_SEND_RESPONSE_HDR_HOOK:
    case TS_HTTP_READ_RESPONSE_HDR_HOOK:
    case TS_HTTP_SEND_REQUEST_HDR_HOOK:
    case TS_HTTP_TXN_CLOSE_HOOK:
      if (TSUrlCreate(req.bufp(), &url->_urlp) == TS_SUCCESS) {
        TSAssert(context->state.txnp);
        if (TSHttpTxnCacheLookupUrlGet(context->state.txnp, req.bufp(), url->_urlp) == TS_SUCCESS) {
          url->_initialize(&context->state, &req);
        }
      } else {
        context->state.error.fail();
      }
      break;
    default: { // This means we have to clone. ToDo: For now, this is implicitly using Client::URL
      Client::URL &src = Client::URL::_get(context);

      if (TSUrlClone(req.bufp(), req.bufp(), src.urlp(), &url->_urlp) == TS_SUCCESS) {
        url->_initialize(&context->state, &req);
      } else {
        context->state.error.fail();
      }
    } break;
    }
  }

  return context->_cache_url;
}

// This has to be implemented here, since the Cript::Context is not complete yet
bool
Cache::URL::_update(Cript::Context *context)
{
  // For correctness, we will also make sure the Path and Query objects are flushed
  path.flush();
  query.flush();

  if (_modified) {
    TSAssert(context->state.txnp);
    _modified = false;
    if (TS_SUCCESS == TSHttpTxnCacheLookupUrlSet(context->state.txnp, _bufp, _urlp)) {
      if (context->p_instance.debugOn()) {
        context->p_instance.debug("Successfully setting cache-key to {}", url());
      }
      return true;
    } else {
      if (context->p_instance.debugOn()) {
        context->p_instance.debug("Could not set the cache key to {}", url());
      }
      context->state.error.fail();
      return false;
    }
  }

  return false;
}

// ToDo: This may need more work, to understand which hooks the parent URL is actually available
Parent::URL &
Parent::URL::_get(Cript::Context *context)
{
  if (!context->_cache_url.initialized()) {
    Parent::URL     *url = &context->_parent_url;
    Client::Request &req = Client::Request::_get(context); // Repurpose / create the shared request object

    if (TSUrlCreate(req.bufp(), &url->_urlp) == TS_SUCCESS) {
      TSAssert(context->state.txnp);
      if (TSHttpTxnParentSelectionUrlGet(context->state.txnp, req.bufp(), url->_urlp) == TS_SUCCESS) {
        url->_initialize(&context->state, &req);
      }
    } else {
      context->state.error.fail();
    }
  }

  return context->_parent_url;
}
