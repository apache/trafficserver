/** @file

  A brief file description

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

//////////////////////////////////////////////////////////////////////////////////////////////
// 
// Implement the resource class (per request), for optimal processing speed.
//
#ifndef __RESOURCES_H__
#define __RESOURCES_H__ 1


#include <ts/remap.h>
#include <ts/ts.h>



///////////////////////////////////////////////////////////////////////////////
// Class declaration
//
class Resources
{
public:
  Resources(TSHttpTxn txnp, TSRemapRequestInfo *rri) :
    _txnp(txnp), _rri(rri), _jar(NULL), _bufp(NULL), _hdrLoc(NULL)
  { }

  ~Resources() {
    if (_hdrLoc) {
      TSDebug("balancer", "Releasing the client request headers");
      TSHandleMLocRelease(_bufp, TS_NULL_MLOC, _hdrLoc);
    }

    if (_jar) {
      TSDebug("balancer", "Destroying the cookie jar");
      // TODO - destroy cookies
    }
  }

  const TSHttpTxn getTxnp() const { return _txnp; }
    
  const TSRemapRequestInfo* getRRI() const { return _rri; }

  const cookiejar_t
  getJar() {
    if (_jar)
      return _jar;

    // Setup the cookie jar for all processing
    if (_rri->request_cookie_size > 0) {
      char cookie_hdr[_rri->request_cookie_size + 1];

      memcpy(cookie_hdr, _rri->request_cookie, _rri->request_cookie_size);
      cookie_hdr[_rri->request_cookie_size] = '\0';
      _jar = // TODO - create cookies
      TSDebug("balancer", "Creating the cookie jar");
    }

    return _jar;
  }

  const TSMBuffer
  getBufp() {
    if (_bufp) {
      return _bufp;
    } else {
      if (!_txnp || !TSHttpTxnClientReqGet(_txnp, &_bufp, &_hdrLoc)) {
        _bufp = NULL;
        _hdrLoc = NULL;
      }
      return _bufp;
    }
  }

  const TSMLoc
  getHdrLoc() {
    if (!_bufp || !_hdrLoc)
      (void)getBufp();

    return _hdrLoc;
  }

private:
  TSHttpTxn _txnp;
  TSRemapRequestInfo* _rri;
  cookiejar_t _jar;
  TSMBuffer _bufp;
  TSMLoc _hdrLoc;
};


#endif // __HASHKEY_H
