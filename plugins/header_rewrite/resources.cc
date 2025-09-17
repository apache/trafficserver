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
//////////////////////////////////////////////////////////////////////////////////////////////
// resources.cc: Implementation of the resources class.
//
//
#include "ts/ts.h"

#include "resources.h"
#include "lulu.h"

#if TS_HAS_CRIPTS
#include "cripts/Connections.hpp"
#endif

// Collect all resources
void
Resources::gather(const ResourceIDs ids, TSHttpHookID hook)
{
  Dbg(pi_dbg_ctl, "Building resources, hook=%s", TSHttpHookNameLookup(hook));

  // Clear the capture groups just in case
  ovector_count = 0;
  ovector_ptr   = nullptr;

  Dbg(pi_dbg_ctl, "Gathering resources for hook %s with IDs %d", TSHttpHookNameLookup(hook), ids);

  // If we need the client request headers, make sure it's also available in the client vars.
  if (ids & RSRC_CLIENT_REQUEST_HEADERS) {
    Dbg(pi_dbg_ctl, "\tAdding TXN client request header buffers");
    if (TSHttpTxnClientReqGet(state.txnp, &client_bufp, &client_hdr_loc) != TS_SUCCESS) {
      Dbg(pi_dbg_ctl, "could not gather bufp/hdr_loc for request");
      return;
    }
  }

  switch (hook) {
  case TS_HTTP_READ_RESPONSE_HDR_HOOK:
    // Read response headers from server
    if ((ids & RSRC_SERVER_RESPONSE_HEADERS) || (ids & RSRC_RESPONSE_STATUS)) {
      Dbg(pi_dbg_ctl, "\tAdding TXN server response header buffers");
      if (TSHttpTxnServerRespGet(state.txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
        Dbg(pi_dbg_ctl, "could not gather bufp/hdr_loc for response");
        return;
      }
      if (ids & RSRC_RESPONSE_STATUS) {
        Dbg(pi_dbg_ctl, "\tAdding TXN server response status resource");
        resp_status = TSHttpHdrStatusGet(bufp, hdr_loc);
      }
    }
    break;

  case TS_HTTP_SEND_REQUEST_HDR_HOOK:
    Dbg(pi_dbg_ctl, "Processing TS_HTTP_SEND_REQUEST_HDR_HOOK");
    // Read request headers to server
    if (ids & RSRC_SERVER_REQUEST_HEADERS) {
      Dbg(pi_dbg_ctl, "\tAdding TXN server request header buffers");
      if (TSHttpTxnServerReqGet(state.txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
        Dbg(pi_dbg_ctl, "could not gather bufp/hdr_loc for request");
        return;
      }
    }
    break;

  case TS_HTTP_READ_REQUEST_HDR_HOOK:
  case TS_HTTP_PRE_REMAP_HOOK:
    // Read request from client
    if (ids & RSRC_CLIENT_REQUEST_HEADERS) {
      bufp    = client_bufp;
      hdr_loc = client_hdr_loc;
    }
    break;

  case TS_HTTP_SEND_RESPONSE_HDR_HOOK:
    // Send response headers to client
    if (ids & RSRC_CLIENT_RESPONSE_HEADERS) {
      Dbg(pi_dbg_ctl, "\tAdding TXN client response header buffers");
      if (TSHttpTxnClientRespGet(state.txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
        Dbg(pi_dbg_ctl, "could not gather bufp/hdr_loc for request");
        return;
      }
      if (ids & RSRC_RESPONSE_STATUS) {
        Dbg(pi_dbg_ctl, "\tAdding TXN client response status resource");
        resp_status = TSHttpHdrStatusGet(bufp, hdr_loc);
      }
    }
    break;

  case TS_REMAP_PSEUDO_HOOK:
    // Pseudo-hook for a remap instance
    if (client_bufp && client_hdr_loc) {
      Dbg(pi_dbg_ctl, "\tAdding TXN client request header buffers for remap instance");
      bufp    = client_bufp;
      hdr_loc = client_hdr_loc;
    }
    break;

  case TS_HTTP_TXN_START_HOOK:
    // Get TCP Info at transaction start
    if (client_bufp && client_hdr_loc) {
      Dbg(pi_dbg_ctl, "\tAdding TXN client request header buffers for TXN Start instance");
      bufp    = client_bufp;
      hdr_loc = client_hdr_loc;
    }
    break;

  case TS_HTTP_TXN_CLOSE_HOOK:
    // Get TCP Info at transaction close
    Dbg(pi_dbg_ctl, "\tAdding TXN close buffers");
    if (TSHttpTxnClientRespGet(state.txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
      Dbg(pi_dbg_ctl, "could not gather bufp/hdr_loc for request");
      return;
    }
    break;

  default:
    break;
  }

  // The following is all the new infrastructure borrowed / reused from
  // the Cripts library.
#if TS_HAS_CRIPTS
  if (ids & (RSRC_CLIENT_CONNECTION | RSRC_MTLS_CERTIFICATE | RSRC_SERVER_CERTIFICATE)) {
    Dbg(pi_dbg_ctl, "\tAdding Cripts Client::Connection");
    client_conn = new cripts::Client::Connection();
    client_conn->set_state(&state);
  }

  if (ids & RSRC_SERVER_CONNECTION) {
    Dbg(pi_dbg_ctl, "\tAdding Cripts Server::Connection");
    server_conn = new cripts::Server::Connection();
    server_conn->set_state(&state);
  }

  if (ids & RSRC_MTLS_CERTIFICATE) {
    Dbg(pi_dbg_ctl, "\tAdding Cripts Certs::Client");
    mtls_cert = new cripts::Certs::Client(*client_conn);
  }

  if (ids & RSRC_SERVER_CERTIFICATE) {
    Dbg(pi_dbg_ctl, "\tAdding Cripts Certs::Server");
    server_cert = new cripts::Certs::Server(*client_conn);
  }
#endif

  _ready = true;
}

void
Resources::destroy()
{
  if (bufp) {
    if (hdr_loc) {
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    }
  }

  if (client_bufp && (client_bufp != bufp)) {
    if (client_hdr_loc && (client_hdr_loc != hdr_loc)) { // TODO: Is this check really necessary?
      TSHandleMLocRelease(client_bufp, TS_NULL_MLOC, client_hdr_loc);
    }
  }

#if TS_HAS_CRIPTS
  delete client_conn;
  delete server_conn;
  delete mtls_cert;
  delete server_cert;
#endif

  _ready = false;
}
