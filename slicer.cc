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

#include "slicer.h"

#include "config.h"

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
/*
  TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);
  range_header_check(txnp);
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
*/
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
  DEBUG_LOG("Slicer Plugin Init");

  if (nullptr == api_info)
  {
    strncpy(errbuf, "[tsremap_init] - Invalid TSRemapInterface argument", errbuf_size - 1);
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

  DEBUG_LOG("slicer remap is successfully initialized.");
  return TS_SUCCESS;
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
	SlicerConfig * const config = new SlicerConfig;
	if (! config->parseArguments(argc, argv, errbuf, errbuf_size))
	{
		DEBUG_LOG("Couldn't parse slicer remap arguments");
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
		SlicerConfig * const config = static_cast<SlicerConfig*>(ih);
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
 * slicer handles GET requests only
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
		ERROR_LOG("Slicer config not available");
		return TSREMAP_NO_REMAP;
	}

	// simple check to handle
	if (! isGetRequest(txnp))
	{
		return TSREMAP_NO_REMAP;
	}

	// configure and set up continuation
	SlicerConfig * const config = static_cast<SlicerConfig*>(ih);

	return TSREMAP_NO_REMAP;
}

/**
 * Transaction event handler.
 */
static
void
transaction_handler
	( TSCont contp
	, TSEvent event
	, void * const edata
	)
{
  TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);
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
