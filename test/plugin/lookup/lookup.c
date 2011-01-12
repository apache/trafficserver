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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ts.h"

/* #define DEBUG     1 */
#define DEBUG_TAG "lookup-dbg"
#define NEG_DEBUG_TAG "lookup-neg"
#define TIMEOUT 10

/**************************************************
   Log macros for error code return verification
**************************************************/
#define PLUGIN_NAME "lookup"
#define VALID_POINTER(X) ((X != NULL) && (X != TS_ERROR_PTR))
#define LOG_SET_FUNCTION_NAME(NAME) const char * FUNCTION_NAME = NAME
#define LOG_ERROR(API_NAME) { \
    TSDebug(PLUGIN_NAME, "%s: %s %s %s File %s, line number %d", PLUGIN_NAME, API_NAME, "APIFAIL", \
	     FUNCTION_NAME, __FILE__, __LINE__); \
}
#define LOG_ERROR_NEG(API_NAME) { \
    TSDebug(PLUGIN_NAME, "%s: %s %s %s File %s, line number %d", PLUGIN_NAME, API_NAME, "NEGAPIFAIL", \
	     FUNCTION_NAME, __FILE__, __LINE__); \
}
#define LOG_ERROR_AND_RETURN(API_NAME) { \
    LOG_ERROR(API_NAME); \
    return -1; \
}
#define LOG_ERROR_AND_CLEANUP(API_NAME) { \
  LOG_ERROR(API_NAME); \
  goto Lcleanup; \
}
#define LOG_ERROR_AND_REENABLE(API_NAME) { \
  LOG_ERROR(API_NAME); \
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE); \
}

/********************************************************
   Macros for retrieving IP@ from the unsigned integer
********************************************************/
#define IP_a(_x) ((ntohl(_x) & 0xFF000000) >> 24)
#define IP_b(_x) ((ntohl(_x) & 0x00FF0000) >> 16)
#define IP_c(_x) ((ntohl(_x) & 0x0000FF00) >> 8)
#define IP_d(_x)  (ntohl(_x) & 0x000000FF)

/******************************************
   Global variables needed by the plugin
******************************************/
const char *HOSTIP_HDR = "Host-IP";
const char *HOSTNAME_HDR = "Hostname";
const char *HOSTNAME_LENGTH_HDR = "Hostname-Length";
static char *HOSTNAME;
static int HOSTNAME_LENGTH;
TSMutex HOSTNAME_LOCK;

/*************************************************
   Structure to store the txn continuation data
*************************************************/
typedef struct
{
  int called_cache;
  int cache_lookup_status;
  int client_port;
  unsigned int ip_address;
  TSHttpTxn txnp;
} ContData;

/*******************************************************/
/* Convert cache lookup status from constant to string */
/*******************************************************/
char *cacheLookupResult[] = {
  "TS_CACHE_LOOKUP_MISS",
  "TS_CACHE_LOOKUP_HIT_STALE",
  "TS_CACHE_LOOKUP_HIT_FRESH",
  "TS_CACHE_LOOKUP_SKIPPED"
};


/**************************************************
   Allocate and initialize the continuation data
**************************************************/
void
initContData(TSCont txn_contp)
{
  LOG_SET_FUNCTION_NAME("initContData");

  ContData *contData;

  contData = (ContData *) TSmalloc(sizeof(ContData));
  contData->called_cache = 0;
  contData->cache_lookup_status = -1;
  contData->client_port = 0;
  contData->ip_address = 0;
  contData->txnp = NULL;
  if (TSContDataSet(txn_contp, contData) == TS_ERROR) {
    LOG_ERROR("TSContDataSet");
  }
}

/**************************************
   Cleanup the txn continuation data
**************************************/
void
destroyContData(TSCont txn_contp)
{
  LOG_SET_FUNCTION_NAME("destroyContData");

  ContData *contData;
  contData = TSContDataGet(txn_contp);
  if (contData == TS_ERROR_PTR) {
    LOG_ERROR("TSContDataGet");
  } else {
    /* txn_contp->txnp = NULL; */
    TSfree(contData);
  }
}

/**************************************
    Negative testing for cache lookup:
    Call the API at a bad hook and
    verify it returns an error
**************************************/
/* Comment out because it seems to be
   working at every hook after
   TS_HTTP_TXN_CACHE_LOOKUP_COMPLETE */
/* void neg_cache_lookup_bad_hook(TSHttpTxn txnp) { */
/*     LOG_SET_FUNCTION_NAME("neg_cache_lookup_bad_hook"); */

/*     int cache_lookup = 0; */
/*     if (TSHttpTxnCacheLookupStatusGet(txnp, &cache_lookup) != TS_ERROR) { */
/* 	LOG_ERROR_NEG("TSHttpTxnCacheLookupStatusGet"); */
/* 	TSDebug(DEBUG_TAG, */
/* 		 "neg_cache_lookup_bad_hook, Got %s\n", cacheLookupResult[cache_lookup]); */
/*     }  */
/* } */

/**************************************
    Negative testing for cache lookup:
    Call the API with bad args and
    verify it returns an error
**************************************/
void
neg_cache_lookup_bad_arg()
{
  LOG_SET_FUNCTION_NAME("neg_cache_lookup_bad_arg");

  int cache_lookup = 0;
  if (TSHttpTxnCacheLookupStatusGet(NULL, &cache_lookup) != TS_ERROR) {
    LOG_ERROR_NEG("TSHttpTxnCacheLookupStatusGet");
  }
}

/**************************************
    Fake handler used for neg test
    neg1_host_lookup
**************************************/
static int
fake_handler1(TSCont fake_contp, TSEvent event, void *edata)
{

  TSDebug(NEG_DEBUG_TAG, "Received event %d", event);
  TSContDestroy(fake_contp);
  return 0;
}

/**************************************
    Fake handler used for neg test
    TSHttpHookAdd
**************************************/
static int
fake_handler2(TSCont fake_contp, TSEvent event, void *edata)
{
  LOG_SET_FUNCTION_NAME("fake_handler2");

  TSDebug(NEG_DEBUG_TAG, "Received event %d", event);
  TSContDestroy(fake_contp);
  LOG_ERROR_NEG("TSHttpHookAdd");
  TSHttpTxnReenable((TSHttpTxn) edata, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

/**************************************
    Negative testing 1 for host lookup:
    Call the TSHostLookup with bad
    arguments and verify it returns an
    error
**************************************/
void
neg1_host_lookup()
{
  LOG_SET_FUNCTION_NAME("neg1_host_lookup");

  TSCont fake_contp1, fake_contp2;
  TSAction pending_action;

  /* Create fake continuations */
  fake_contp1 = TSContCreate(fake_handler1, TSMutexCreate());
  fake_contp2 = TSContCreate(fake_handler1, TSMutexCreate());

  /* call with NULL continuation */
  pending_action = TSHostLookup(NULL, HOSTNAME, HOSTNAME_LENGTH);
  if (pending_action != TS_ERROR_PTR) {
    LOG_ERROR_NEG("TSHostLookup");
  } else {
    /* Call with NULL hostname */
    pending_action = TSHostLookup(fake_contp1, NULL, HOSTNAME_LENGTH);
    if (pending_action != TS_ERROR_PTR) {
      LOG_ERROR_NEG("TSHostLookup");
    } else {
      /* Destroy fake_contp1 */
      TSContDestroy(fake_contp1);

      /* Call with a 0 HOSTNAME_LENGTH */
      /* Use different continuation to call this API because it is reentrant,
         i.e. we might use fake_contp1 while it has already been destroyed */
      pending_action = TSHostLookup(fake_contp2, HOSTNAME, 0);
      if (pending_action != TS_ERROR_PTR) {
        LOG_ERROR_NEG("TSHostLookup");
      } else {
        /* Destroy fake_contp2 */
        TSContDestroy(fake_contp2);
      }
    }
  }
}

/**************************************
    Negative testing 2 for host lookup:
    Call the TSHostLookupResultIPGet
    with NULL lookup result and verify
    it returns an error
**************************************/
void
neg2_host_lookup()
{
  LOG_SET_FUNCTION_NAME("neg2_host_lookup");

  unsigned int ip;

  /* call with NULL lookup result */
  if (TSHostLookupResultIPGet(NULL, &ip) != TS_ERROR) {
    LOG_ERROR_NEG("TSHostLookupResultIPGet");
  }
}

/**************************************************
    This function is called to verify that the
    value returned by TSHttpTxnClientRemotePortGet
    remains consistent along the HTTP state machine
***************************************************/
int
check_client_port(TSHttpTxn txnp, ContData * contData)
{
  LOG_SET_FUNCTION_NAME("check_client_port");

  int clientPortGot = 0;

  /* TSHttpTxnClientRemotePortGet  */
  if (TSHttpTxnClientRemotePortGet(txnp, &clientPortGot) == TS_ERROR) {
    LOG_ERROR_AND_RETURN("TSHttpTxnClientRemotePortGet");
  } else {
    TSDebug(DEBUG_TAG, "TSHttpTxnClientRemotePortGet returned %d", clientPortGot);
    /* Make sure the client port was set at Read_request hook, to avoid
       firing the assert because the client aborted */
    if (contData->client_port != 0) {
      if (clientPortGot != contData->client_port) {
        TSDebug(DEBUG_TAG, "Bad client port: Expected %d, Got %d", contData->client_port, clientPortGot);
        TSReleaseAssert(!"TSHttpTxnClientRemotePortGet returned bad client port");
      }
    }
  }

/* NEGATIVE TEST for client port */
#ifdef DEBUG
  if (TSHttpTxnClientRemotePortGet(NULL, &clientPortGot) != TS_ERROR) {
    LOG_ERROR_NEG("TSHttpTxnClientRemotePortGet");
  }
#endif

  return 0;
}

/***************************************************
   Release the txn_contp data and destroy it
***************************************************/
int
handle_txn_close(TSHttpTxn txnp, TSCont txn_contp)
{
  LOG_SET_FUNCTION_NAME("handle_txn_close");

/* NEGATIVE TEST for cache lookup */
#ifdef DEBUG
/*     neg_cache_lookup_bad_hook(txnp); */
/*     TSDebug(DEBUG_TAG, */
/* 	     "NEGATIVE test cache lookup bad hook in %s passed", FUNCTION_NAME); */
  neg_cache_lookup_bad_arg();
#endif

  destroyContData(txn_contp);
  if (TSContDestroy(txn_contp) == TS_ERROR) {
    LOG_ERROR("TSContDestroy");
  }

  return 0;
}

/***************************************************
   Insert the Host-IP header in the client response
***************************************************/
int
handle_send_response(TSHttpTxn txnp, ContData * contData)
{
  LOG_SET_FUNCTION_NAME("handle_send_response");

  char ipGot[100];
  char temp[25];
  TSMBuffer respBuf;
  TSMLoc respLoc, hostIPLoc;

  /* Check Client Port is consistent */
  check_client_port(txnp, contData);

  /*  Get client response */
  if (!TSHttpTxnClientRespGet(txnp, &respBuf, &respLoc)) {
    LOG_ERROR_AND_RETURN("TSHttpTxnClientRespGet");
  }

  /* Create Host-IP header */
  hostIPLoc = TSMimeHdrFieldCreate(respBuf, respLoc);
  if (hostIPLoc == TS_ERROR_PTR) {
    LOG_ERROR_AND_CLEANUP("TSMimeHdrFieldCreate");
  }
  /* Append Host-IP hdr to client response */
  if (TSMimeHdrFieldAppend(respBuf, respLoc, hostIPLoc) == TS_ERROR) {
    LOG_ERROR_AND_CLEANUP("TSMimeHdrFieldAppend");
  }
  /* Set Host-IP hdr Name */
  if (TSMimeHdrFieldNameSet(respBuf, respLoc, hostIPLoc, HOSTIP_HDR, strlen(HOSTIP_HDR))) {
    LOG_ERROR_AND_CLEANUP("TSMimeHdrFieldNameSet");
  }
  /* Get Host-IP hdr value from unsigned integer to a string */
  if (contData->ip_address != 0) {
    sprintf(ipGot, "%u", IP_a(contData->ip_address));
    strcat(ipGot, ".");
    sprintf(temp, "%u", IP_b(contData->ip_address));
    strcat(ipGot, temp);
    strcat(ipGot, ".");
    sprintf(temp, "%u", IP_c(contData->ip_address));
    strcat(ipGot, temp);
    strcat(ipGot, ".");
    sprintf(temp, "%u", IP_d(contData->ip_address));
    strcat(ipGot, temp);
    TSDebug(DEBUG_TAG, "IP@ = %s", ipGot);
  } else {
    sprintf(ipGot, "%d", 0);
  }
  /* Set Host-IP hdr value */
  if (TSMimeHdrFieldValueStringSet(respBuf, respLoc, hostIPLoc, -1, ipGot, -1) == TS_ERROR) {
    LOG_ERROR("TSMimeHdrFieldValueStringSet");
  }

Lcleanup:
  if (VALID_POINTER(hostIPLoc)) {
    if (TSHandleMLocRelease(respBuf, respLoc, hostIPLoc) == TS_ERROR) {
      LOG_ERROR("TSHandleMLocRelease");
    }
  }
  if (VALID_POINTER(respLoc)) {
    if (TSHandleMLocRelease(respBuf, TS_NULL_MLOC, respLoc) == TS_ERROR) {
      LOG_ERROR("TSHandleMLocRelease");
    }
  }

/* NEGATIVE TEST for cache lookup */
#ifdef DEBUG
/*     neg_cache_lookup_bad_hook(txnp); */
/*     TSDebug(DEBUG_TAG, */
/* 	     "NEGATIVE test cache lookup bad hook in %s passed", FUNCTION_NAME); */
  neg_cache_lookup_bad_arg();
#endif

  return 0;
}

/***************************************************
   Verify client port
   Verify cache lookup is consistent
***************************************************/
int
handle_read_response(TSHttpTxn txnp, ContData * contData)
{
  LOG_SET_FUNCTION_NAME("handle_read_response");

  /* Check Client Port is consistent */
  check_client_port(txnp, contData);

  /* Verify cache_lookup_status is consistent */
  /* Should not be a TS_CACHE_LOOKUP_HIT_FRESH */
  TSReleaseAssert(contData->cache_lookup_status != TS_CACHE_LOOKUP_HIT_FRESH);

/* NEGATIVE TEST for cache lookup */
#ifdef DEBUG
/*     neg_cache_lookup_bad_hook(txnp); */
/*     TSDebug(DEBUG_TAG, */
/* 	     "NEGATIVE test cache lookup bad hook in %s passed", FUNCTION_NAME); */
  neg_cache_lookup_bad_arg();
#endif

  return 0;
}

/***************************************************
   Verify client port
   Verify cache lookup is consistent
***************************************************/
int
handle_send_request(TSHttpTxn txnp, ContData * contData)
{
  LOG_SET_FUNCTION_NAME("handle_send_request");

  /* Check Client Port is consistent */
  check_client_port(txnp, contData);

  /* Verify cache_lookup_status is consistent */
  /* Should not be a TS_CACHE_LOOKUP_HIT_FRESH */
  TSReleaseAssert(contData->cache_lookup_status != TS_CACHE_LOOKUP_HIT_FRESH);

/* NEGATIVE TEST for cache lookup */
#ifdef DEBUG
/*     neg_cache_lookup_bad_hook(txnp); */
/*     TSDebug(DEBUG_TAG, */
/* 	     "NEGATIVE test cache lookup bad hook in %s passed", FUNCTION_NAME); */
  neg_cache_lookup_bad_arg();
#endif

  return 0;
}

/***************************************************
   Do the cache lookup and check state machine
   Set cache_lookup_status
***************************************************/
int
handle_cache_lookup_complete(TSHttpTxn txnp, ContData * contData)
{
  LOG_SET_FUNCTION_NAME("handle_cache_lookup_complete");

  /* Check Client Port is consistent */
  check_client_port(txnp, contData);

    /****************/
  /* Cache Lookup */
    /****************/
  /* Get cache lookup status */
  if (TSHttpTxnCacheLookupStatusGet(txnp, &contData->cache_lookup_status) == TS_ERROR) {
    LOG_ERROR_AND_RETURN("TSHttpTxnCacheLookupStatusGet");
  }
  TSDebug(DEBUG_TAG, "Got cache lookup status %s", cacheLookupResult[contData->cache_lookup_status]);
  /* Verify cache_lookup_status and called_cache are consistent */
  if (contData->cache_lookup_status == TS_CACHE_LOOKUP_MISS) {
    /* called_cache should not be set */
    TSReleaseAssert(!contData->called_cache);
  } else if (contData->cache_lookup_status == TS_CACHE_LOOKUP_HIT_STALE) {
    /* called_cache should be set */
    TSReleaseAssert(contData->called_cache);
  } else if (contData->cache_lookup_status == TS_CACHE_LOOKUP_HIT_FRESH) {
    /* called_cache should be set */
    TSReleaseAssert(contData->called_cache);
  } else if (contData->cache_lookup_status == TS_CACHE_LOOKUP_SKIPPED) {
    /* called_cache should not be set */
    TSReleaseAssert(!contData->called_cache);
  } else
    TSReleaseAssert(!"Bad Cache Lookup Status");

/* NEGATIVE TEST for cache lookup */
#ifdef DEBUG
  neg_cache_lookup_bad_arg();
#endif

  return 0;
}

/***************************************************
   Verify client port
   Set called_cache
***************************************************/
int
handle_read_cache(TSHttpTxn txnp, ContData * contData)
{
  LOG_SET_FUNCTION_NAME("handle_read_cache");

  /* Check Client Port is consistent */
  check_client_port(txnp, contData);

  /* Set "called_cache" in the txn_contp data */
  contData->called_cache = 1;

/* NEGATIVE TEST for cache lookup */
#ifdef DEBUG
/*     neg_cache_lookup_bad_hook(txnp); */
/*     TSDebug(DEBUG_TAG, */
/* 	     "NEGATIVE test cache lookup bad hook in %s passed", FUNCTION_NAME); */
  neg_cache_lookup_bad_arg();
#endif

  return 0;
}

/***************************************************
   Set client_port
   Parse HOSTNAME and HOSTNAME-LENGTH from client
   request
   Call TSHostLookup
***************************************************/
int
handle_read_request(TSHttpTxn txnp, TSCont txn_contp)
{
  LOG_SET_FUNCTION_NAME("handle_read_request");

  int clientPortGot = 0;
  const char *hostnameString = NULL, *hostnameLengthString = NULL;
  int hostnameStringLength = 0, hostnameLengthStringLength = 0;
  TSMBuffer clientReqBuf;
  TSMLoc clientReqLoc, hostnameLoc, hostnameLengthLoc;
  ContData *contData;

  /* Get client request */
  if (!TSHttpTxnClientReqGet(txnp, &clientReqBuf, &clientReqLoc)) {
    LOG_ERROR_AND_RETURN("TSHttpTxnClientReqGet");
  }

    /***************/
  /* Client port */
    /***************/
  /* Get txn_contp data pointer */
  contData = TSContDataGet(txn_contp);
  if (contData == TS_ERROR_PTR) {
    LOG_ERROR("TSContDataGet");
  } else {
    /* TSHttpTxnClientRemotePortGet  */
    if (TSHttpTxnClientRemotePortGet(txnp, &contData->client_port) == TS_ERROR) {
      LOG_ERROR_AND_CLEANUP("TSHttpTxnClientRemotePortGet");
    } else {
      TSDebug(DEBUG_TAG, "TSHttpTxnClientRemotePortGet returned %d", contData->client_port);
    }
  }

/* NEGATIVE TEST for client port, cache lookup and host lookup */
#ifdef DEBUG
  if (TSHttpTxnClientRemotePortGet(NULL, &clientPortGot) != TS_ERROR) {
    LOG_ERROR_NEG("TSHttpTxnClientRemotePortGet");
  }

/*     neg_cache_lookup_bad_hook(txnp); */
/*     TSDebug(DEBUG_TAG, */
/* 	     "NEGATIVE test cache lookup bad hook in %s passed", FUNCTION_NAME); */
  neg_cache_lookup_bad_arg();

  neg1_host_lookup(txn_contp);

  neg2_host_lookup();

#endif

    /*****************/
  /* TSHostLookup */
    /*****************/
  /* Get "Hostname" header */
  hostnameLoc = TSMimeHdrFieldFind(clientReqBuf, clientReqLoc, HOSTNAME_HDR, strlen(HOSTNAME_HDR));
  if (hostnameLoc == TS_ERROR_PTR) {
    LOG_ERROR("TSMimeHdrFieldFind");
  } else if (!hostnameLoc) {
    /* Client did not send header, use default */
    TSDebug(DEBUG_TAG, "No Hostname header in client's request");
  } else {
    /* Get the hostname value. If fails use default */
    if (TSMimeHdrFieldValueStringGet
        (clientReqBuf, clientReqLoc, hostnameLoc, 0, &hostnameString, &hostnameStringLength) == TS_ERROR) {
      LOG_ERROR("TSMimeHdrFieldValueStringGet");
    } else if ((!hostnameString) || (hostnameStringLength == 0)) {
      /* Client sent header without value  */
      TSDebug(DEBUG_TAG, "No Hostname header value in client's request");
    } else {
      /* Set HOSTNAME */
      if (TSMutexLock(HOSTNAME_LOCK) == TS_ERROR) {
        LOG_ERROR("TSMutexLock");
      }
      /* 1rst free the previous TSstrdup */
      TSfree(HOSTNAME);
      HOSTNAME = TSstrdup(hostnameString);
      TSMutexUnlock(HOSTNAME_LOCK);
    }
  }
  /* Get "Hostname-Length" header */
  hostnameLengthLoc = TSMimeHdrFieldFind(clientReqBuf, clientReqLoc, HOSTNAME_LENGTH_HDR, strlen(HOSTNAME_LENGTH_HDR));
  if (hostnameLengthLoc == TS_ERROR_PTR) {
    LOG_ERROR("TSMimeHdrFieldFind");
  } else if (!hostnameLengthLoc) {
    /* Client did not send header, use default */
    TSDebug(DEBUG_TAG, "No Hostname-Length header in client's request");
  } else {
    /* Get the hostname length value. If fails use default */
    if (TSMimeHdrFieldValueStringGet
        (clientReqBuf, clientReqLoc, hostnameLengthLoc, 0, &hostnameLengthString,
         &hostnameLengthStringLength) == TS_ERROR) {
      LOG_ERROR("TSMimeHdrFieldValueStringGet");
    } else if ((!hostnameLengthString) || (hostnameLengthStringLength == 0)) {
      /* Client sent header without value  */
      TSDebug(DEBUG_TAG, "No Hostname-Length header value in client's request");
    } else {
      if (!isnan(atoi(hostnameLengthString))) {
        /* Set HOSTNAME_LENGTH */
        if (TSMutexLock(HOSTNAME_LOCK) == TS_ERROR) {
          LOG_ERROR("TSMutexLock");
        }
        HOSTNAME_LENGTH = atoi(hostnameLengthString);
        TSMutexUnlock(HOSTNAME_LOCK);
      }
    }
  }

Lcleanup:
  if (VALID_POINTER(hostnameLoc)) {
    if (TSHandleMLocRelease(clientReqBuf, clientReqLoc, hostnameLoc) == TS_ERROR) {
      LOG_ERROR("TSHandleMLocRelease");
    }
  }
  if (VALID_POINTER(hostnameLengthLoc)) {
    if (TSHandleMLocRelease(clientReqBuf, clientReqLoc, hostnameLengthLoc) == TS_ERROR) {
      LOG_ERROR("TSHandleMLocRelease");
    }
  }
  if (VALID_POINTER(clientReqLoc)) {
    if (TSHandleMLocRelease(clientReqBuf, TS_NULL_MLOC, clientReqLoc) == TS_ERROR) {
      LOG_ERROR("TSHandleMLocRelease");
    }
  }

  /* Call TSHostLookup */
  /* Called completly at the end because right after the call,
     the DNS processor might call back txn_contp wih the
     TS_EVENT_HOST_LOOKUP, and txnp will be reenabled while
     txnp is still being accessed in this handler, that would
     be bad!!! */
  if (TSHostLookup(txn_contp, HOSTNAME, HOSTNAME_LENGTH) == TS_ERROR_PTR) {
    LOG_ERROR("TSHostLookup");
  }
  /* Do nothing after this call, return right away */
  return 0;
}

/**********************************************************************
   Txn continuation handler:
   Each HTTP transaction creates its continuation to do the following:
   - registers itself (local registration) for all subsequent hooks
   - be called back by the DNS processor when the host lookup is done
   - store all the transaction specific data
   Tricks:
   - when called back with TS_EVENT_HTTP_READ_REQUEST_HDR, do not
   reenable the transaction, instead return, and reenable the
   transaction when called back with TS_EVENT_HOST_LOOKUP, so that
   when we don't need to maintain a state in the continuation. And
   also when we need the host lookup result at the send response hook
   we are sure that the result will be available.
   - we don't need a lock for this continuation because we are garanteed
   that we will be called back for only one HTTP hook at a time, and the
   asynchronous part (host lookup)
**********************************************************************/
static int
txn_cont_handler(TSCont txn_contp, TSEvent event, void *edata)
{
  LOG_SET_FUNCTION_NAME("hostlookup");

  TSHostLookupResult result;
  TSHttpTxn txnp;
  ContData *contData;

  contData = TSContDataGet(txn_contp);

  switch (event) {
        /***************/
    /* HTTP events */
        /***************/
  case TS_EVENT_HTTP_READ_REQUEST_HDR:
    txnp = (TSHttpTxn) edata;
    handle_read_request(txnp, txn_contp);
    return 0;
  case TS_EVENT_HTTP_READ_CACHE_HDR:
    txnp = (TSHttpTxn) edata;
    if (contData == TS_ERROR_PTR) {
      LOG_ERROR("TSContDataGet");
    } else {
      handle_read_cache(txnp, contData);
    }
    break;
  case TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE:
    txnp = (TSHttpTxn) edata;
    handle_cache_lookup_complete(txnp, contData);
    break;
  case TS_EVENT_HTTP_SEND_REQUEST_HDR:
    txnp = (TSHttpTxn) edata;
    handle_send_request(txnp, contData);
    break;
  case TS_EVENT_HTTP_READ_RESPONSE_HDR:
    txnp = (TSHttpTxn) edata;
    handle_read_response(txnp, contData);
    break;
  case TS_EVENT_HTTP_SEND_RESPONSE_HDR:
    txnp = (TSHttpTxn) edata;
    if (contData == TS_ERROR_PTR) {
      LOG_ERROR("TSContDataGet");
    } else {
      handle_send_response(txnp, contData);
    }
    break;
  case TS_EVENT_HTTP_TXN_CLOSE:
    txnp = (TSHttpTxn) edata;
    handle_txn_close(txnp, txn_contp);
    break;

        /*********************/
    /* Host Lookup event */
        /*********************/
  case TS_EVENT_HOST_LOOKUP:
    if (contData == TS_ERROR_PTR) {
      /* In this case we are stuck, we cannot get the continuation
         data which contains the HTTP txn pointer, i.e. we cannot
         reenable the transaction, so we might as well assert here */
      LOG_ERROR("TSContDataGet");
      TSReleaseAssert(!"Could not get contp data");
    }
    txnp = contData->txnp;
    result = (TSHostLookupResult) edata;
    if (result != NULL) {
      /* Get the IP@ */
      if (TSHostLookupResultIPGet(result, &contData->ip_address) == TS_ERROR) {
        LOG_ERROR("TSHostLookupResultIPGet");
      }
    } else {
      TSDebug(DEBUG_TAG, "Hostlookup continuation called back with NULL result");
    }
/* NEGATIVE TEST for host lookup */
#ifdef DEBUG
    /* Comment out because of TSqa12283 */
    neg1_host_lookup(txn_contp);
    neg2_host_lookup();
#endif
    break;

  default:
    TSAssert(!"Unexpected Event");
    break;
  }

  if (TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE) == TS_ERROR) {
    LOG_ERROR("TSHttpTxnReenable");
  }

  return 0;
}

/***************************************************************
    When the global plugin continuation is called back here:
    - for every HTTP txn, it creates a continuation
    - init the continuation data
    - set the transaction pointer to the mother HTTP txn. This
    pointer is part of the daughter continuation's data.
    - registers the new continuation to be called back for all
    other HTTP hooks.
***************************************************************/
int
handle_txn_start(TSHttpTxn txnp)
{
  LOG_SET_FUNCTION_NAME("handle_txn_start");

  TSMBuffer fake_mbuffer;
  TSCont txn_contp, fake_contp;
  ContData *contData, *fakeData;
  TSMutex mutexp;

  /* Create mutex for new txn_contp */
  mutexp = TSMutexCreate();
  if (mutexp == TS_ERROR_PTR) {
    LOG_ERROR_AND_RETURN("TSMutexCreate");
  }
  /* Create the HTTP txn continuation */
  txn_contp = TSContCreate(txn_cont_handler, mutexp);
  if (txn_contp == TS_ERROR_PTR) {
    LOG_ERROR_AND_RETURN("TSContCreate");
  }
  /* Init this continuation data  */
  initContData(txn_contp);
  /* Get this continuation data */
  contData = TSContDataGet(txn_contp);
  if (contData == TS_ERROR_PTR) {
    LOG_ERROR_AND_RETURN("TSContDataGet");
  }
  /* Set the transaction pointer */
  contData->txnp = txnp;

  /* Add the TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK to this transaction  */
  if (TSHttpTxnHookAdd(txnp, TS_HTTP_READ_REQUEST_HDR_HOOK, txn_contp) == TS_ERROR) {
    LOG_ERROR("TSHttpTxnHookAdd");
  }
  if (TSHttpTxnHookAdd(txnp, TS_HTTP_READ_CACHE_HDR_HOOK, txn_contp) == TS_ERROR) {
    LOG_ERROR("TSHttpTxnHookAdd");
  }
  if (TSHttpTxnHookAdd(txnp, TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, txn_contp) == TS_ERROR) {
    LOG_ERROR("TSHttpTxnHookAdd");
  }
  if (TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_REQUEST_HDR_HOOK, txn_contp) == TS_ERROR) {
    LOG_ERROR("TSHttpTxnHookAdd");
  }
  if (TSHttpTxnHookAdd(txnp, TS_HTTP_READ_RESPONSE_HDR_HOOK, txn_contp) == TS_ERROR) {
    LOG_ERROR("TSHttpTxnHookAdd");
  }
  if (TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_RESPONSE_HDR_HOOK, txn_contp) == TS_ERROR) {
    LOG_ERROR("TSHttpTxnHookAdd");
  }
  if (TSHttpTxnHookAdd(txnp, TS_HTTP_TXN_CLOSE_HOOK, txn_contp) == TS_ERROR) {
    LOG_ERROR("TSHttpTxnHookAdd");
  }

/* NEGATIVE TEST for cache lookup, TSHttpTxnHookAdd,
   TSMutexLock, TSMutexLockTry, TSMutexUnlock,
   TSHandleMLocRelease and TSHttpTxnReenable */
#ifdef DEBUG
/*     neg_cache_lookup_bad_hook(txnp); */
/*     TSDebug(DEBUG_TAG, */
/* 	     "NEGATIVE test cache lookup bad hook in %s passed", FUNCTION_NAME); */
  neg_cache_lookup_bad_arg();

  if (TSHttpTxnHookAdd(NULL, -1, NULL) != TS_ERROR) {
    LOG_ERROR_NEG("TSHttpTxnHookAdd");
  } else
    TSDebug(NEG_DEBUG_TAG, "Neg Test TSHttpTxnHookAdd 1 passed\n");
  fake_contp = TSContCreate(fake_handler2, TSMutexCreate());
  if (TSHttpTxnHookAdd(NULL, TS_HTTP_TXN_START_HOOK, NULL) != TS_ERROR) {
    LOG_ERROR_NEG("TSHttpTxnHookAdd");
  } else
    TSDebug(NEG_DEBUG_TAG, "Neg Test TSHttpTxnHookAdd 2  passed\n");
  if (TSHttpTxnHookAdd(NULL, -1, fake_contp) != TS_ERROR) {
    LOG_ERROR_NEG("TSHttpTxnHookAdd");
  } else
    TSDebug(NEG_DEBUG_TAG, "Neg Test TSHttpTxnHookAdd 3  passed\n");
  if (TSMutexLock(NULL) != TS_ERROR) {
    LOG_ERROR_NEG("TSMutexLock");
  } else
    TSDebug(NEG_DEBUG_TAG, "Neg Test TSMutexLock passed\n");
  if (TSMutexLockTry(NULL, NULL) != TS_ERROR) {
    LOG_ERROR_NEG("TSMutexLockTry");
  } else
    TSDebug(NEG_DEBUG_TAG, "Neg Test TSMutexLockTry passed\n");
  if (TSMutexUnlock(NULL) != TS_ERROR) {
    LOG_ERROR_NEG("TSMutexUnlock");
  } else
    TSDebug(NEG_DEBUG_TAG, "Neg Test TSMutexUnlock passed\n");
  if (TSHandleMLocRelease(NULL, TS_NULL_MLOC, NULL)) {
    LOG_ERROR_NEG("TSHandleMLocRelease");
  } else
    TSDebug(NEG_DEBUG_TAG, "Neg Test TSHandleMLocRelease 1 passed\n");
  fake_mbuffer = TSMBufferCreate();
  if (TSHandleMLocRelease(fake_mbuffer, TS_NULL_MLOC, NULL)) {
    LOG_ERROR_NEG("TSHandleMLocRelease");
  } else
    TSDebug(NEG_DEBUG_TAG, "Neg Test TSHandleMLocRelease 1 passed\n");
  if (TSHttpTxnReenable(NULL, 0) != TS_ERROR) {
    LOG_ERROR_NEG("TSHttpTxnReenable");
  } else
    TSDebug(NEG_DEBUG_TAG, "Neg Test TSHttpTxnReenable passed\n");
#endif

  return 0;
}

/********************************************************
   Plugin Continuation handler:
   The plugin continuation will be called back by every
   HTTP transaction when it reach TS_HTTP_TXN_START_HOOK
********************************************************/
static int
plugin_cont_handler(TSCont contp, TSEvent event, void *edata)
{
  LOG_SET_FUNCTION_NAME("process_plugin");

  TSHttpTxn txnp = (TSHttpTxn) edata;

  switch (event) {
  case TS_EVENT_HTTP_TXN_START:
    handle_txn_start(txnp);
    break;

  default:
    TSAssert(!"Unexpected Event");
    break;
  }

  if (TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE) == TS_ERROR) {
    LOG_ERROR("TSHttpTxnReenable");
  }

  return 0;
}

/********************************************************
   Parse the 2 eventual arguments passed to the plugin,
   else use the defaults.
   Here, there is no need to grab the HOSTNAME_LOCK, this
   code should be executes before any HTTP state machine
   is created.
   Register globally TS_HTTP_TXN_START_HOOK
********************************************************/
void
TSPluginInit(int argc, const char *argv[])
{
  LOG_SET_FUNCTION_NAME("TSPluginInit");

  /* Plugin continuation */
  TSCont contp, fake_contp;
  ContData *fakeData;

  /* Create hostname lock */
  HOSTNAME_LOCK = TSMutexCreate();

  /* Initialize global variables hostname and hostname length  */
  /* No need to grab the lock here  */
  HOSTNAME = TSstrdup("www.example.com");
  HOSTNAME_LENGTH = strlen(HOSTNAME) + 1;

  /* Parse the eventual 2 plugin arguments */
  if (argc < 3) {
    TSDebug(DEBUG_TAG, "Usage: lookup.so hostname hostname_length");
    printf("[lookup_plugin] Usage: lookup.so hostname hostname_length\n");
    printf("[lookup_plugin] Wrong arguments. Using default\n");
  } else {
    HOSTNAME = TSstrdup(argv[1]);
    TSDebug(DEBUG_TAG, "using hostname %s", HOSTNAME);
    printf("[lookup_plugin] using hostname %s\n", HOSTNAME);

    if (!isnan(atoi(argv[2]))) {
      HOSTNAME_LENGTH = atoi(argv[2]);
      TSDebug(DEBUG_TAG, "using hostname length %d", HOSTNAME_LENGTH);
      printf("[lookup_plugin] using hostname length %d\n", HOSTNAME_LENGTH);
    } else {
      printf("[lookup_plugin] Wrong argument for hostname length");
      printf("Using default hostname length %d\n", HOSTNAME_LENGTH);
    }
  }

/* Negative test for TSContCreate, TSHttpHookAdd, TSContDataGet/Set, TSContDestroy */
#ifdef DEBUG
  if (TSHttpHookAdd(-1, NULL) != TS_ERROR) {
    LOG_ERROR_NEG("TSHttpHookAdd");
  } else
    TSDebug(NEG_DEBUG_TAG, "Neg Test TSHttpHookAdd 1 passed\n");
  if (TSHttpHookAdd(TS_HTTP_TXN_START_HOOK, NULL) != TS_ERROR) {
    LOG_ERROR_NEG("TSHttpHookAdd");
  } else
    TSDebug(NEG_DEBUG_TAG, "Neg Test TSHttpHookAdd 2 passed\n");
  fake_contp = TSContCreate(fake_handler2, TSMutexCreate());
  if (TSHttpHookAdd(-1, fake_contp) != TS_ERROR) {
    LOG_ERROR_NEG("TSHttpHookAdd");
  } else
    TSDebug(NEG_DEBUG_TAG, "Neg Test TSHttpHookAdd 3 passed\n");
  if (TSContDataGet(NULL) != TS_ERROR_PTR) {
    LOG_ERROR_NEG("TSContDataGet");
  } else
    TSDebug(NEG_DEBUG_TAG, "Neg Test TSContDataGet passed\n");
  if (TSContDataSet(NULL, NULL) != TS_ERROR) {
    LOG_ERROR_NEG("TSContDataSet");
  } else
    TSDebug(NEG_DEBUG_TAG, "Neg Test TSContDataSet 1 passed\n");
  fakeData = (ContData *) TSmalloc(sizeof(ContData));
  fakeData->called_cache = 0;
  fakeData->cache_lookup_status = -1;
  fakeData->client_port = 0;
  fakeData->ip_address = 0;
  fakeData->txnp = NULL;
  if (TSContDataSet(NULL, fakeData) != TS_ERROR) {
    LOG_ERROR_NEG("TSContDataSet");
  } else
    TSDebug(NEG_DEBUG_TAG, "Neg Test TSContDataSet 3 passed\n");
  if (TSContDestroy(NULL) != TS_ERROR) {
    LOG_ERROR_NEG("TSContDestroy");
  } else
    TSDebug(NEG_DEBUG_TAG, "Neg Test TSContDestroy passed\n");
#endif

  if ((contp = TSContCreate(plugin_cont_handler, NULL)) == TS_ERROR_PTR) {
    LOG_ERROR("TSContCreate");
  } else {
    if (TSHttpHookAdd(TS_HTTP_TXN_START_HOOK, contp) == TS_ERROR) {
      LOG_ERROR("TSHttpHookAdd");
    }
  }
}
