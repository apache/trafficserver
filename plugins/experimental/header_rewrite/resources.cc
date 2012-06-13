//////////////////////////////////////////////////////////////////////////////////////////////
// resources.cc: Implementation of the resources class.
//
//

#define UNUSED __attribute__ ((unused))
static char UNUSED rcsId__resources_cc[] = "@(#) $Id$ built on " __DATE__ " " __TIME__;

#include <ts/ts.h>

#include "resources.h"
#include "lulu.h"

void
Resources::gather(const ResourceIDs ids, TSHttpHookID hook)
{
  TSDebug(PLUGIN_NAME, "Building resource structure for hook (%d)", hook);

  // If we need the client request headers, make sure it's also available in the client vars.
  if (ids & RSRC_CLIENT_REQUEST_HEADERS) {
    TSDebug(PLUGIN_NAME, "\tAdding TXN client request header buffers");
    if (TSHttpTxnClientReqGet(txnp, &client_bufp, &client_hdr_loc) != TS_SUCCESS) {
      TSDebug(PLUGIN_NAME, "could not gather bufp/hdr_loc for request");
      return;
    }
  }

  switch (hook) {
  case TS_HTTP_READ_RESPONSE_HDR_HOOK:
    // Read response headers from server
    if (ids & RSRC_SERVER_RESPONSE_HEADERS) {
      TSDebug(PLUGIN_NAME, "\tAdding TXN server response header buffers");
      if (TSHttpTxnServerRespGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
        TSDebug(PLUGIN_NAME, "could not gather bufp/hdr_loc for response");
        return;
      }
    }
    if (ids & RSRC_RESPONSE_STATUS) {
      TSDebug(PLUGIN_NAME, "\tAdding TXN server response status resource");
      resp_status = TSHttpHdrStatusGet(bufp, hdr_loc);
    }
    break;

  case TS_HTTP_SEND_REQUEST_HDR_HOOK:
    // Read request headers to server
    if (ids & RSRC_SERVER_REQUEST_HEADERS) {
      TSDebug(PLUGIN_NAME, "\tAdding TXN server request header buffers");
      if (!TSHttpTxnServerReqGet(txnp, &bufp, &hdr_loc)) {
        TSDebug(PLUGIN_NAME, "could not gather bufp/hdr_loc for request");
        return;
      }
    }
    break;

  case TS_HTTP_READ_REQUEST_HDR_HOOK:
  case TS_HTTP_READ_REQUEST_PRE_REMAP_HOOK:
    // Read request from client
    if (ids & RSRC_CLIENT_REQUEST_HEADERS) {
      bufp = client_bufp;
      hdr_loc = client_hdr_loc;
    }
    break;

  case TS_HTTP_SEND_RESPONSE_HDR_HOOK:
    // Send response headers to client
    if (ids & RSRC_CLIENT_RESPONSE_HEADERS) {
      TSDebug(PLUGIN_NAME, "\tAdding TXN client response header buffers");
      if (TSHttpTxnClientRespGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
        TSDebug(PLUGIN_NAME, "could not gather bufp/hdr_loc for request");
        return;
      }
      if (ids & RSRC_RESPONSE_STATUS) {
        TSDebug(PLUGIN_NAME, "\tAdding TXN client esponse status resource");
        resp_status = TSHttpHdrStatusGet(bufp, hdr_loc);
      }
    }
    break;

  case TS_REMAP_PSEUDO_HOOK:
    // Pseudo-hook for a remap instance
    if (client_bufp && client_hdr_loc) {
      TSDebug(PLUGIN_NAME, "\tAdding TXN client request header buffers for remap instance");
      bufp = client_bufp;
      hdr_loc = client_hdr_loc;
    }
    break;
    
    
  default:
    break;
  }

    _ready  = true;
}

void
Resources::destroy()
{
  if (bufp) {
    if (hdr_loc)
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
  }

  if (client_bufp && (client_bufp != bufp)) {
    if (client_hdr_loc && (client_hdr_loc != hdr_loc)) // TODO: Is this check really necessary?
      TSHandleMLocRelease(client_bufp, TS_NULL_MLOC, client_hdr_loc);
  }

  _ready = false;
}
