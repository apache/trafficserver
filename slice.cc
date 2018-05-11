/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

/*
 * This plugin looks for range requests and then creates a new
 * cache key url so that each individual range requests is written
 * to the cache as a individual object so that subsequent range
 * requests are read accross different disk drives reducing I/O
 * wait and load averages when there are large numbers of range
 * requests.
 */

#include "slice.h"

#include "config.h"
#include "data.h"

#include "ts/remap.h"

#include <cassert>
#include <cstdio>

/**
 * Entry point when used as a global plugin.
 */
static
void
handle_read_request_header
	( TSCont txn_contp
	, TSEvent event
	, void * edata
	)
{
	DEBUG_LOG("Global plugin handle begin");
/*
  TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);
  range_header_check(txnp);
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
*/
}

/**
 * continuation handler
 */
static
int
cont_handler
	( TSCont contp
	, TSEvent event
	, void * edata
	)
{
	DEBUG_LOG("cont_handler: %s", TSHttpEventNameLookup(event));

	switch (event)
	{
		case TS_EVENT_HTTP_TXN_CLOSE:
		{
			DEBUG_LOG("transaction close");
			SliceData * const slicedata
				= static_cast<SliceData*>(TSContDataGet(contp));
			delete slicedata;
			TSContDestroy(contp);
//			TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
		}
		break;
		default:
		{
//			TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
		}
		break;
	}

	return 0;
}

/**
 * Remap initialization.
 */
SLICER_EXPORT
TSReturnCode
TSRemapInit
	( TSRemapInterface * api_info
	, char * errbuf
	, int errbuf_size
	)
{
  DEBUG_LOG("Slice Plugin Init");

  if (nullptr == api_info)
  {
    strncpy
	 	( errbuf
		, "[tsremap_init] - Invalid TSRemapInterface argument"
		, errbuf_size - 1);
    return TS_ERROR;
  }

  if (api_info->tsremap_version < TSREMAP_VERSION)
  {
    snprintf
	 	( errbuf
		, errbuf_size
		, "[TSRemapInit] - Incorrect API version %ld.%ld"
		, api_info->tsremap_version >> 16
		, api_info->tsremap_version & 0xffff );
    return TS_ERROR;
  }

  DEBUG_LOG("slice remap is successfully initialized.");
  return TS_SUCCESS;
}

/**
 * Origin server response
 */
SLICER_EXPORT
void
TSRemapOSResponse
	( void * ih
	, TSHttpTxn txn
	, int os_response_type
	)
{
	DEBUG_LOG("Origin Server Response");
}

/**
 * Initialize the configuration based on remap options
 */
SLICER_EXPORT
TSReturnCode
TSRemapNewInstance
	( int argc
	, char * argv[]
	, void ** ih
	, char * errbuf
	, int errbuf_size
	)
{
	DEBUG_LOG("New Instance");
	SliceConfig * const config = new SliceConfig;
	if (! config->parseArguments(argc, argv, errbuf, errbuf_size))
	{
		DEBUG_LOG("Couldn't parse slice remap arguments");
		delete config;
		return TS_ERROR;
	}

	*ih = static_cast<void*>(config);

	return TS_SUCCESS;
}

/**
 * Delete the configuration based on remap options
 */
SLICER_EXPORT
void
TSRemapDeleteInstance
	 ( void * ih
	 )
{
	DEBUG_LOG("Delete Instance");
	if (nullptr != ih)
	{
		SliceConfig * const config = static_cast<SliceConfig*>(ih);
		delete config;
	}
}

/**
 * guard to make sure a TSMLoc is deallocated with scope.
 */
struct GuardMloc
{
	TSMBuffer & buf;
	TSMLoc & loc;

	explicit
	GuardMloc
		( TSMBuffer & _bufin
		, TSMLoc & _locin
		)
		: buf(_bufin)
		, loc(_locin)
	{ }

	~GuardMloc
		()
	{
		TSHandleMLocRelease(buf, TS_NULL_MLOC, loc);
	}
};

/**
 * slice handles GET requests only
 */
static
bool
isGetRequest
	( TSHttpTxn const & txnp
	)
{
	// intercept GET requests only
	TSMBuffer reqbuf;
	TSMLoc reqloc; // allocated by call

	TSReturnCode rc(TSHttpTxnClientReqGet(txnp, &reqbuf, &reqloc));
	if (TS_SUCCESS != rc)
	{
		return TSREMAP_NO_REMAP;
	}

	// takes responsibility for reqloc memory
	GuardMloc const reqrai(reqbuf, reqloc);

	int match_len(0);
	char const * method = TSHttpHdrMethodGet(reqbuf, reqloc, &match_len);
	if ( NULL == method || 3 != match_len || 0 != strcmp("GET", method) )
	{
		if (NULL == method)
		{
			DEBUG_LOG("No method found in header");
		}
		else
		{
			DEBUG_LOG("Method %s not handled", method);
		}
		return false;
	}

	return true;
}

/**
 * Entry point for slicing.
 */
SLICER_EXPORT
TSRemapStatus
TSRemapDoRemap
	( void * ih
	, TSHttpTxn txnp
	, TSRemapRequestInfo * // rri
	)
{
	DEBUG_LOG("TSRemapDoRemap hit");

	if (nullptr == ih)
	{
		ERROR_LOG("Slice config not available");
		return TSREMAP_NO_REMAP;
	}

	// simple check to handle
	if (! isGetRequest(txnp))
	{
		return TSREMAP_NO_REMAP;
	}

	// configure and set up continuation
	SliceConfig * const sliceconfig = static_cast<SliceConfig*>(ih);

	// slice data with view into the config
	SliceData * const slicedata = new SliceData(sliceconfig, txnp);

	// set up our continuation
	TSCont contp = TSContCreate(cont_handler, NULL);
	TSContDataSet(contp, static_cast<void*>(slicedata));

	// add in hooks of interest

	// set up transform hook if necessary
	TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_RESPONSE_HDR_HOOK, contp);

	// transaction close, cleanup
	TSHttpTxnHookAdd(txnp, TS_HTTP_TXN_CLOSE_HOOK, contp);

	return TSREMAP_NO_REMAP;
}

/**
 * Global plugin initialization.
 */
SLICER_EXPORT
void
TSPluginInit
	( int // argc
	, char const * /* argv */[]
	)
{
  TSPluginRegistrationInfo info;
  info.plugin_name   = (char *)PLUGIN_NAME;
  info.vendor_name   = (char *)"Comcast";
  info.support_email = (char *)"brian_olsen2@comcast.com";

  if (TS_SUCCESS != TSPluginRegister(&info))
  {
    ERROR_LOG("Plugin registration failed.\n");
    ERROR_LOG("Unable to initialize plugin (disabled).");
    return;
  }

  TSCont const txnp_cont
    ( TSContCreate
	 	( (TSEventFunc)handle_read_request_header
		, nullptr ) );

  if (nullptr == txnp_cont)
  {
    ERROR_LOG("failed to create the transaction continuation handler.");
    return;
  }
  else
  {
    TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, txnp_cont);
  }
}
