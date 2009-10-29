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
#include "InkAPI.h"

/* #define DEBUG     1 */
#define DEBUG_TAG "lookup-dbg"
#define NEG_DEBUG_TAG "lookup-neg"
#define TIMEOUT 10

/**************************************************
   Log macros for error code return verification 
**************************************************/
#define PLUGIN_NAME "lookup"
#define VALID_POINTER(X) ((X != NULL) && (X != INK_ERROR_PTR))
#define LOG_SET_FUNCTION_NAME(NAME) const char * FUNCTION_NAME = NAME
#define LOG_ERROR(API_NAME) { \
    INKDebug(PLUGIN_NAME, "%s: %s %s %s File %s, line number %d", PLUGIN_NAME, API_NAME, "APIFAIL", \
	     FUNCTION_NAME, __FILE__, __LINE__); \
}
#define LOG_ERROR_NEG(API_NAME) { \
    INKDebug(PLUGIN_NAME, "%s: %s %s %s File %s, line number %d", PLUGIN_NAME, API_NAME, "NEGAPIFAIL", \
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
  INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE); \
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
INKMutex HOSTNAME_LOCK;

/*************************************************
   Structure to store the txn continuation data
*************************************************/
typedef struct
{
  int called_cache;
  int cache_lookup_status;
  int client_port;
  unsigned int ip_address;
  INKHttpTxn txnp;
} ContData;

/*******************************************************/
/* Convert cache lookup status from constant to string */
/*******************************************************/
char *cacheLookupResult[] = {
  "INK_CACHE_LOOKUP_MISS",
  "INK_CACHE_LOOKUP_HIT_STALE",
  "INK_CACHE_LOOKUP_HIT_FRESH",
  "INK_CACHE_LOOKUP_SKIPPED"
};


/**************************************************
   Allocate and initialize the continuation data
**************************************************/
void
initContData(INKCont txn_contp)
{
  LOG_SET_FUNCTION_NAME("initContData");

  ContData *contData;

  contData = (ContData *) INKmalloc(sizeof(ContData));
  contData->called_cache = 0;
  contData->cache_lookup_status = -1;
  contData->client_port = 0;
  contData->ip_address = 0;
  contData->txnp = NULL;
  if (INKContDataSet(txn_contp, contData) == INK_ERROR) {
    LOG_ERROR("INKContDataSet");
  }
}

/**************************************
   Cleanup the txn continuation data
**************************************/
void
destroyContData(INKCont txn_contp)
{
  LOG_SET_FUNCTION_NAME("destroyContData");

  ContData *contData;
  contData = INKContDataGet(txn_contp);
  if (contData == INK_ERROR_PTR) {
    LOG_ERROR("INKContDataGet");
  } else {
    /* txn_contp->txnp = NULL; */
    INKfree(contData);
  }
}

/**************************************
    Negative testing for cache lookup:
    Call the API at a bad hook and
    verify it returns an error
**************************************/
/* Comment out because it seems to be 
   working at every hook after 
   INK_HTTP_TXN_CACHE_LOOKUP_COMPLETE */
/* void neg_cache_lookup_bad_hook(INKHttpTxn txnp) { */
/*     LOG_SET_FUNCTION_NAME("neg_cache_lookup_bad_hook"); */

/*     int cache_lookup = 0; */
/*     if (INKHttpTxnCacheLookupStatusGet(txnp, &cache_lookup) != INK_ERROR) { */
/* 	LOG_ERROR_NEG("INKHttpTxnCacheLookupStatusGet"); */
/* 	INKDebug(DEBUG_TAG, */
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
  if (INKHttpTxnCacheLookupStatusGet(NULL, &cache_lookup) != INK_ERROR) {
    LOG_ERROR_NEG("INKHttpTxnCacheLookupStatusGet");
  }
}

/**************************************
    Fake handler used for neg test
    neg1_host_lookup
**************************************/
static int
fake_handler1(INKCont fake_contp, INKEvent event, void *edata)
{

  INKDebug(NEG_DEBUG_TAG, "Received event %d", event);
  INKContDestroy(fake_contp);
  return 0;
}

/**************************************
    Fake handler used for neg test
    INKHttpHookAdd
**************************************/
static int
fake_handler2(INKCont fake_contp, INKEvent event, void *edata)
{
  LOG_SET_FUNCTION_NAME("fake_handler2");

  INKDebug(NEG_DEBUG_TAG, "Received event %d", event);
  INKContDestroy(fake_contp);
  LOG_ERROR_NEG("INKHttpHookAdd");
  INKHttpTxnReenable((INKHttpTxn) edata, INK_EVENT_HTTP_CONTINUE);
  return 0;
}

/**************************************
    Negative testing 1 for host lookup:
    Call the INKHostLookup with bad 
    arguments and verify it returns an 
    error
**************************************/
void
neg1_host_lookup()
{
  LOG_SET_FUNCTION_NAME("neg1_host_lookup");

  INKCont fake_contp1, fake_contp2;
  INKAction pending_action;

  /* Create fake continuations */
  fake_contp1 = INKContCreate(fake_handler1, INKMutexCreate());
  fake_contp2 = INKContCreate(fake_handler1, INKMutexCreate());

  /* call with NULL continuation */
  pending_action = INKHostLookup(NULL, HOSTNAME, HOSTNAME_LENGTH);
  if (pending_action != INK_ERROR_PTR) {
    LOG_ERROR_NEG("INKHostLookup");
  } else {
    /* Call with NULL hostname */
    pending_action = INKHostLookup(fake_contp1, NULL, HOSTNAME_LENGTH);
    if (pending_action != INK_ERROR_PTR) {
      LOG_ERROR_NEG("INKHostLookup");
    } else {
      /* Destroy fake_contp1 */
      INKContDestroy(fake_contp1);

      /* Call with a 0 HOSTNAME_LENGTH */
      /* Use different continuation to call this API because it is reentrant, 
         i.e. we might use fake_contp1 while it has already been destroyed */
      pending_action = INKHostLookup(fake_contp2, HOSTNAME, 0);
      if (pending_action != INK_ERROR_PTR) {
        LOG_ERROR_NEG("INKHostLookup");
      } else {
        /* Destroy fake_contp2 */
        INKContDestroy(fake_contp2);
      }
    }
  }
}

/**************************************
    Negative testing 2 for host lookup:
    Call the INKHostLookupResultIPGet 
    with NULL lookup result and verify 
    it returns an error
**************************************/
void
neg2_host_lookup()
{
  LOG_SET_FUNCTION_NAME("neg2_host_lookup");

  unsigned int ip;

  /* call with NULL lookup result */
  if (INKHostLookupResultIPGet(NULL, &ip) != INK_ERROR) {
    LOG_ERROR_NEG("INKHostLookupResultIPGet");
  }
}

/**************************************************
    This function is called to verify that the 
    value returned by INKHttpTxnClientRemotePortGet 
    remains consistent along the HTTP state machine
***************************************************/
int
check_client_port(INKHttpTxn txnp, ContData * contData)
{
  LOG_SET_FUNCTION_NAME("check_client_port");

  int clientPortGot = 0;

  /* INKHttpTxnClientRemotePortGet  */
  if (INKHttpTxnClientRemotePortGet(txnp, &clientPortGot) == INK_ERROR) {
    LOG_ERROR_AND_RETURN("INKHttpTxnClientRemotePortGet");
  } else {
    INKDebug(DEBUG_TAG, "INKHttpTxnClientRemotePortGet returned %d", clientPortGot);
    /* Make sure the client port was set at Read_request hook, to avoid 
       firing the assert because the client aborted */
    if (contData->client_port != 0) {
      if (clientPortGot != contData->client_port) {
        INKDebug(DEBUG_TAG, "Bad client port: Expected %d, Got %d", contData->client_port, clientPortGot);
        INKReleaseAssert(!"INKHttpTxnClientRemotePortGet returned bad client port");
      }
    }
  }

/* NEGATIVE TEST for client port */
#ifdef DEBUG
  if (INKHttpTxnClientRemotePortGet(NULL, &clientPortGot) != INK_ERROR) {
    LOG_ERROR_NEG("INKHttpTxnClientRemotePortGet");
  }
#endif

  return 0;
}

/***************************************************
   Release the txn_contp data and destroy it
***************************************************/
int
handle_txn_close(INKHttpTxn txnp, INKCont txn_contp)
{
  LOG_SET_FUNCTION_NAME("handle_txn_close");

/* NEGATIVE TEST for cache lookup */
#ifdef DEBUG
/*     neg_cache_lookup_bad_hook(txnp); */
/*     INKDebug(DEBUG_TAG, */
/* 	     "NEGATIVE test cache lookup bad hook in %s passed", FUNCTION_NAME); */
  neg_cache_lookup_bad_arg();
#endif

  destroyContData(txn_contp);
  if (INKContDestroy(txn_contp) == INK_ERROR) {
    LOG_ERROR("INKContDestroy");
  }

  return 0;
}

/***************************************************
   Insert the Host-IP header in the client response 
***************************************************/
int
handle_send_response(INKHttpTxn txnp, ContData * contData)
{
  LOG_SET_FUNCTION_NAME("handle_send_response");

  char ipGot[100];
  char temp[25];
  INKMBuffer respBuf;
  INKMLoc respLoc, hostIPLoc;

  /* Check Client Port is consistent */
  check_client_port(txnp, contData);

  /*  Get client response */
  if (!INKHttpTxnClientRespGet(txnp, &respBuf, &respLoc)) {
    LOG_ERROR_AND_RETURN("INKHttpTxnClientRespGet");
  }

  /* Create Host-IP header */
  hostIPLoc = INKMimeHdrFieldCreate(respBuf, respLoc);
  if (hostIPLoc == INK_ERROR_PTR) {
    LOG_ERROR_AND_CLEANUP("INKMimeHdrFieldCreate");
  }
  /* Append Host-IP hdr to client response */
  if (INKMimeHdrFieldAppend(respBuf, respLoc, hostIPLoc) == INK_ERROR) {
    LOG_ERROR_AND_CLEANUP("INKMimeHdrFieldAppend");
  }
  /* Set Host-IP hdr Name */
  if (INKMimeHdrFieldNameSet(respBuf, respLoc, hostIPLoc, HOSTIP_HDR, strlen(HOSTIP_HDR))) {
    LOG_ERROR_AND_CLEANUP("INKMimeHdrFieldNameSet");
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
    INKDebug(DEBUG_TAG, "IP@ = %s", ipGot);
  } else {
    sprintf(ipGot, "%d", 0);
  }
  /* Set Host-IP hdr value */
  if (INKMimeHdrFieldValueStringSet(respBuf, respLoc, hostIPLoc, -1, ipGot, -1) == INK_ERROR) {
    LOG_ERROR("INKMimeHdrFieldValueStringSet");
  }

Lcleanup:
  if (VALID_POINTER(hostIPLoc)) {
    if (INKHandleMLocRelease(respBuf, respLoc, hostIPLoc) == INK_ERROR) {
      LOG_ERROR("INKHandleMLocRelease");
    }
  }
  if (VALID_POINTER(respLoc)) {
    if (INKHandleMLocRelease(respBuf, INK_NULL_MLOC, respLoc) == INK_ERROR) {
      LOG_ERROR("INKHandleMLocRelease");
    }
  }

/* NEGATIVE TEST for cache lookup */
#ifdef DEBUG
/*     neg_cache_lookup_bad_hook(txnp); */
/*     INKDebug(DEBUG_TAG, */
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
handle_read_response(INKHttpTxn txnp, ContData * contData)
{
  LOG_SET_FUNCTION_NAME("handle_read_response");

  /* Check Client Port is consistent */
  check_client_port(txnp, contData);

  /* Verify cache_lookup_status is consistent */
  /* Should not be a INK_CACHE_LOOKUP_HIT_FRESH */
  INKReleaseAssert(contData->cache_lookup_status != INK_CACHE_LOOKUP_HIT_FRESH);

/* NEGATIVE TEST for cache lookup */
#ifdef DEBUG
/*     neg_cache_lookup_bad_hook(txnp); */
/*     INKDebug(DEBUG_TAG, */
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
handle_send_request(INKHttpTxn txnp, ContData * contData)
{
  LOG_SET_FUNCTION_NAME("handle_send_request");

  /* Check Client Port is consistent */
  check_client_port(txnp, contData);

  /* Verify cache_lookup_status is consistent */
  /* Should not be a INK_CACHE_LOOKUP_HIT_FRESH */
  INKReleaseAssert(contData->cache_lookup_status != INK_CACHE_LOOKUP_HIT_FRESH);

/* NEGATIVE TEST for cache lookup */
#ifdef DEBUG
/*     neg_cache_lookup_bad_hook(txnp); */
/*     INKDebug(DEBUG_TAG, */
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
handle_cache_lookup_complete(INKHttpTxn txnp, ContData * contData)
{
  LOG_SET_FUNCTION_NAME("handle_cache_lookup_complete");

  /* Check Client Port is consistent */
  check_client_port(txnp, contData);

    /****************/
  /* Cache Lookup */
    /****************/
  /* Get cache lookup status */
  if (INKHttpTxnCacheLookupStatusGet(txnp, &contData->cache_lookup_status) == INK_ERROR) {
    LOG_ERROR_AND_RETURN("INKHttpTxnCacheLookupStatusGet");
  }
  INKDebug(DEBUG_TAG, "Got cache lookup status %s", cacheLookupResult[contData->cache_lookup_status]);
  /* Verify cache_lookup_status and called_cache are consistent */
  if (contData->cache_lookup_status == INK_CACHE_LOOKUP_MISS) {
    /* called_cache should not be set */
    INKReleaseAssert(!contData->called_cache);
  } else if (contData->cache_lookup_status == INK_CACHE_LOOKUP_HIT_STALE) {
    /* called_cache should be set */
    INKReleaseAssert(contData->called_cache);
  } else if (contData->cache_lookup_status == INK_CACHE_LOOKUP_HIT_FRESH) {
    /* called_cache should be set */
    INKReleaseAssert(contData->called_cache);
  } else if (contData->cache_lookup_status == INK_CACHE_LOOKUP_SKIPPED) {
    /* called_cache should not be set */
    INKReleaseAssert(!contData->called_cache);
  } else
    INKReleaseAssert(!"Bad Cache Lookup Status");

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
handle_read_cache(INKHttpTxn txnp, ContData * contData)
{
  LOG_SET_FUNCTION_NAME("handle_read_cache");

  /* Check Client Port is consistent */
  check_client_port(txnp, contData);

  /* Set "called_cache" in the txn_contp data */
  contData->called_cache = 1;

/* NEGATIVE TEST for cache lookup */
#ifdef DEBUG
/*     neg_cache_lookup_bad_hook(txnp); */
/*     INKDebug(DEBUG_TAG, */
/* 	     "NEGATIVE test cache lookup bad hook in %s passed", FUNCTION_NAME); */
  neg_cache_lookup_bad_arg();
#endif

  return 0;
}

/***************************************************
   Set client_port
   Parse HOSTNAME and HOSTNAME-LENGTH from client
   request
   Call INKHostLookup
***************************************************/
int
handle_read_request(INKHttpTxn txnp, INKCont txn_contp)
{
  LOG_SET_FUNCTION_NAME("handle_read_request");

  int clientPortGot = 0;
  const char *hostnameString = NULL, *hostnameLengthString = NULL;
  int hostnameStringLength = 0, hostnameLengthStringLength = 0;
  INKMBuffer clientReqBuf;
  INKMLoc clientReqLoc, hostnameLoc, hostnameLengthLoc;
  ContData *contData;

  /* Get client request */
  if (!INKHttpTxnClientReqGet(txnp, &clientReqBuf, &clientReqLoc)) {
    LOG_ERROR_AND_RETURN("INKHttpTxnClientReqGet");
  }

    /***************/
  /* Client port */
    /***************/
  /* Get txn_contp data pointer */
  contData = INKContDataGet(txn_contp);
  if (contData == INK_ERROR_PTR) {
    LOG_ERROR("INKContDataGet");
  } else {
    /* INKHttpTxnClientRemotePortGet  */
    if (INKHttpTxnClientRemotePortGet(txnp, &contData->client_port) == INK_ERROR) {
      LOG_ERROR_AND_CLEANUP("INKHttpTxnClientRemotePortGet");
    } else {
      INKDebug(DEBUG_TAG, "INKHttpTxnClientRemotePortGet returned %d", contData->client_port);
    }
  }

/* NEGATIVE TEST for client port, cache lookup and host lookup */
#ifdef DEBUG
  if (INKHttpTxnClientRemotePortGet(NULL, &clientPortGot) != INK_ERROR) {
    LOG_ERROR_NEG("INKHttpTxnClientRemotePortGet");
  }

/*     neg_cache_lookup_bad_hook(txnp); */
/*     INKDebug(DEBUG_TAG, */
/* 	     "NEGATIVE test cache lookup bad hook in %s passed", FUNCTION_NAME); */
  neg_cache_lookup_bad_arg();

  neg1_host_lookup(txn_contp);

  neg2_host_lookup();

#endif

    /*****************/
  /* INKHostLookup */
    /*****************/
  /* Get "Hostname" header */
  hostnameLoc = INKMimeHdrFieldFind(clientReqBuf, clientReqLoc, HOSTNAME_HDR, strlen(HOSTNAME_HDR));
  if (hostnameLoc == INK_ERROR_PTR) {
    LOG_ERROR("INKMimeHdrFieldFind");
  } else if (!hostnameLoc) {
    /* Client did not send header, use default */
    INKDebug(DEBUG_TAG, "No Hostname header in client's request");
  } else {
    /* Get the hostname value. If fails use default */
    if (INKMimeHdrFieldValueStringGet
        (clientReqBuf, clientReqLoc, hostnameLoc, 0, &hostnameString, &hostnameStringLength) == INK_ERROR) {
      LOG_ERROR("INKMimeHdrFieldValueStringGet");
    } else if ((!hostnameString) || (hostnameStringLength == 0)) {
      /* Client sent header without value  */
      INKDebug(DEBUG_TAG, "No Hostname header value in client's request");
    } else {
      /* Set HOSTNAME */
      if (INKMutexLock(HOSTNAME_LOCK) == INK_ERROR) {
        LOG_ERROR("INKMutexLock");
      }
      /* 1rst free the previous INKstrdup */
      INKfree(HOSTNAME);
      HOSTNAME = INKstrdup(hostnameString);
      INKMutexUnlock(HOSTNAME_LOCK);
    }
  }
  /* Get "Hostname-Length" header */
  hostnameLengthLoc = INKMimeHdrFieldFind(clientReqBuf, clientReqLoc, HOSTNAME_LENGTH_HDR, strlen(HOSTNAME_LENGTH_HDR));
  if (hostnameLengthLoc == INK_ERROR_PTR) {
    LOG_ERROR("INKMimeHdrFieldFind");
  } else if (!hostnameLengthLoc) {
    /* Client did not send header, use default */
    INKDebug(DEBUG_TAG, "No Hostname-Length header in client's request");
  } else {
    /* Get the hostname length value. If fails use default */
    if (INKMimeHdrFieldValueStringGet
        (clientReqBuf, clientReqLoc, hostnameLengthLoc, 0, &hostnameLengthString,
         &hostnameLengthStringLength) == INK_ERROR) {
      LOG_ERROR("INKMimeHdrFieldValueStringGet");
    } else if ((!hostnameLengthString) || (hostnameLengthStringLength == 0)) {
      /* Client sent header without value  */
      INKDebug(DEBUG_TAG, "No Hostname-Length header value in client's request");
    } else {
      if (!isnan(atoi(hostnameLengthString))) {
        /* Set HOSTNAME_LENGTH */
        if (INKMutexLock(HOSTNAME_LOCK) == INK_ERROR) {
          LOG_ERROR("INKMutexLock");
        }
        HOSTNAME_LENGTH = atoi(hostnameLengthString);
        INKMutexUnlock(HOSTNAME_LOCK);
      }
    }
  }

Lcleanup:
  if (VALID_POINTER(hostnameString)) {
    if (INKHandleStringRelease(clientReqBuf, hostnameLoc, hostnameString) == INK_ERROR) {
      LOG_ERROR("INKHandleStringRelease");
    }
  }
  if (VALID_POINTER(hostnameLengthString)) {
    if (INKHandleStringRelease(clientReqBuf, hostnameLengthLoc, hostnameLengthString) == INK_ERROR) {
      LOG_ERROR("INKHandleStringRelease");
    }
  }
  if (VALID_POINTER(hostnameLoc)) {
    if (INKHandleMLocRelease(clientReqBuf, clientReqLoc, hostnameLoc) == INK_ERROR) {
      LOG_ERROR("INKHandleMLocRelease");
    }
  }
  if (VALID_POINTER(hostnameLengthLoc)) {
    if (INKHandleMLocRelease(clientReqBuf, clientReqLoc, hostnameLengthLoc) == INK_ERROR) {
      LOG_ERROR("INKHandleMLocRelease");
    }
  }
  if (VALID_POINTER(clientReqLoc)) {
    if (INKHandleMLocRelease(clientReqBuf, INK_NULL_MLOC, clientReqLoc) == INK_ERROR) {
      LOG_ERROR("INKHandleMLocRelease");
    }
  }

  /* Call INKHostLookup */
  /* Called completly at the end because right after the call, 
     the DNS processor might call back txn_contp wih the 
     INK_EVENT_HOST_LOOKUP, and txnp will be reenabled while
     txnp is still being accessed in this handler, that would
     be bad!!! */
  if (INKHostLookup(txn_contp, HOSTNAME, HOSTNAME_LENGTH) == INK_ERROR_PTR) {
    LOG_ERROR("INKHostLookup");
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
   - when called back with INK_EVENT_HTTP_READ_REQUEST_HDR, do not 
   reenable the transaction, instead return, and reenable the 
   transaction when called back with INK_EVENT_HOST_LOOKUP, so that 
   when we don't need to maintain a state in the continuation. And
   also when we need the host lookup result at the send response hook
   we are sure that the result will be available.
   - we don't need a lock for this continuation because we are garanteed 
   that we will be called back for only one HTTP hook at a time, and the
   asynchronous part (host lookup) 
**********************************************************************/
static int
txn_cont_handler(INKCont txn_contp, INKEvent event, void *edata)
{
  LOG_SET_FUNCTION_NAME("hostlookup");

  INKHostLookupResult result;
  INKHttpTxn txnp;
  ContData *contData;

  contData = INKContDataGet(txn_contp);

  switch (event) {
        /***************/
    /* HTTP events */
        /***************/
  case INK_EVENT_HTTP_READ_REQUEST_HDR:
    txnp = (INKHttpTxn) edata;
    handle_read_request(txnp, txn_contp);
    return 0;
  case INK_EVENT_HTTP_READ_CACHE_HDR:
    txnp = (INKHttpTxn) edata;
    if (contData == INK_ERROR_PTR) {
      LOG_ERROR("INKContDataGet");
    } else {
      handle_read_cache(txnp, contData);
    }
    break;
  case INK_EVENT_HTTP_CACHE_LOOKUP_COMPLETE:
    txnp = (INKHttpTxn) edata;
    handle_cache_lookup_complete(txnp, contData);
    break;
  case INK_EVENT_HTTP_SEND_REQUEST_HDR:
    txnp = (INKHttpTxn) edata;
    handle_send_request(txnp, contData);
    break;
  case INK_EVENT_HTTP_READ_RESPONSE_HDR:
    txnp = (INKHttpTxn) edata;
    handle_read_response(txnp, contData);
    break;
  case INK_EVENT_HTTP_SEND_RESPONSE_HDR:
    txnp = (INKHttpTxn) edata;
    if (contData == INK_ERROR_PTR) {
      LOG_ERROR("INKContDataGet");
    } else {
      handle_send_response(txnp, contData);
    }
    break;
  case INK_EVENT_HTTP_TXN_CLOSE:
    txnp = (INKHttpTxn) edata;
    handle_txn_close(txnp, txn_contp);
    break;

        /*********************/
    /* Host Lookup event */
        /*********************/
  case INK_EVENT_HOST_LOOKUP:
    if (contData == INK_ERROR_PTR) {
      /* In this case we are stuck, we cannot get the continuation 
         data which contains the HTTP txn pointer, i.e. we cannot 
         reenable the transaction, so we might as well assert here */
      LOG_ERROR("INKContDataGet");
      INKReleaseAssert(!"Could not get contp data");
    }
    txnp = contData->txnp;
    result = (INKHostLookupResult) edata;
    if (result != NULL) {
      /* Get the IP@ */
      if (INKHostLookupResultIPGet(result, &contData->ip_address) == INK_ERROR) {
        LOG_ERROR("INKHostLookupResultIPGet");
      }
    } else {
      INKDebug(DEBUG_TAG, "Hostlookup continuation called back with NULL result");
    }
/* NEGATIVE TEST for host lookup */
#ifdef DEBUG
    /* Comment out because of INKqa12283 */
    neg1_host_lookup(txn_contp);
    neg2_host_lookup();
#endif
    break;

  default:
    INKAssert(!"Unexpected Event");
    break;
  }

  if (INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE) == INK_ERROR) {
    LOG_ERROR("INKHttpTxnReenable");
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
handle_txn_start(INKHttpTxn txnp)
{
  LOG_SET_FUNCTION_NAME("handle_txn_start");

  INKMBuffer fake_mbuffer;
  INKCont txn_contp, fake_contp;
  ContData *contData, *fakeData;
  INKMutex mutexp;

  /* Create mutex for new txn_contp */
  mutexp = INKMutexCreate();
  if (mutexp == INK_ERROR_PTR) {
    LOG_ERROR_AND_RETURN("INKMutexCreate");
  }
  /* Create the HTTP txn continuation */
  txn_contp = INKContCreate(txn_cont_handler, mutexp);
  if (txn_contp == INK_ERROR_PTR) {
    LOG_ERROR_AND_RETURN("INKContCreate");
  }
  /* Init this continuation data  */
  initContData(txn_contp);
  /* Get this continuation data */
  contData = INKContDataGet(txn_contp);
  if (contData == INK_ERROR_PTR) {
    LOG_ERROR_AND_RETURN("INKContDataGet");
  }
  /* Set the transaction pointer */
  contData->txnp = txnp;

  /* Add the INK_HTTP_CACHE_LOOKUP_COMPLETE_HOOK to this transaction  */
  if (INKHttpTxnHookAdd(txnp, INK_HTTP_READ_REQUEST_HDR_HOOK, txn_contp) == INK_ERROR) {
    LOG_ERROR("INKHttpTxnHookAdd");
  }
  if (INKHttpTxnHookAdd(txnp, INK_HTTP_READ_CACHE_HDR_HOOK, txn_contp) == INK_ERROR) {
    LOG_ERROR("INKHttpTxnHookAdd");
  }
  if (INKHttpTxnHookAdd(txnp, INK_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, txn_contp) == INK_ERROR) {
    LOG_ERROR("INKHttpTxnHookAdd");
  }
  if (INKHttpTxnHookAdd(txnp, INK_HTTP_SEND_REQUEST_HDR_HOOK, txn_contp) == INK_ERROR) {
    LOG_ERROR("INKHttpTxnHookAdd");
  }
  if (INKHttpTxnHookAdd(txnp, INK_HTTP_READ_RESPONSE_HDR_HOOK, txn_contp) == INK_ERROR) {
    LOG_ERROR("INKHttpTxnHookAdd");
  }
  if (INKHttpTxnHookAdd(txnp, INK_HTTP_SEND_RESPONSE_HDR_HOOK, txn_contp) == INK_ERROR) {
    LOG_ERROR("INKHttpTxnHookAdd");
  }
  if (INKHttpTxnHookAdd(txnp, INK_HTTP_TXN_CLOSE_HOOK, txn_contp) == INK_ERROR) {
    LOG_ERROR("INKHttpTxnHookAdd");
  }

/* NEGATIVE TEST for cache lookup, INKHttpTxnHookAdd,
   INKMutexLock, INKMutexLockTry, INKMutexUnlock,
   INKHandleMLocRelease and INKHttpTxnReenable */
#ifdef DEBUG
/*     neg_cache_lookup_bad_hook(txnp); */
/*     INKDebug(DEBUG_TAG, */
/* 	     "NEGATIVE test cache lookup bad hook in %s passed", FUNCTION_NAME); */
  neg_cache_lookup_bad_arg();

  if (INKHttpTxnHookAdd(NULL, -1, NULL) != INK_ERROR) {
    LOG_ERROR_NEG("INKHttpTxnHookAdd");
  } else
    INKDebug(NEG_DEBUG_TAG, "Neg Test INKHttpTxnHookAdd 1 passed\n");
  fake_contp = INKContCreate(fake_handler2, INKMutexCreate());
  if (INKHttpTxnHookAdd(NULL, INK_HTTP_TXN_START_HOOK, NULL) != INK_ERROR) {
    LOG_ERROR_NEG("INKHttpTxnHookAdd");
  } else
    INKDebug(NEG_DEBUG_TAG, "Neg Test INKHttpTxnHookAdd 2  passed\n");
  if (INKHttpTxnHookAdd(NULL, -1, fake_contp) != INK_ERROR) {
    LOG_ERROR_NEG("INKHttpTxnHookAdd");
  } else
    INKDebug(NEG_DEBUG_TAG, "Neg Test INKHttpTxnHookAdd 3  passed\n");
  if (INKMutexLock(NULL) != INK_ERROR) {
    LOG_ERROR_NEG("INKMutexLock");
  } else
    INKDebug(NEG_DEBUG_TAG, "Neg Test INKMutexLock passed\n");
  if (INKMutexLockTry(NULL, NULL) != INK_ERROR) {
    LOG_ERROR_NEG("INKMutexLockTry");
  } else
    INKDebug(NEG_DEBUG_TAG, "Neg Test INKMutexTryLock passed\n");
  if (INKMutexUnlock(NULL) != INK_ERROR) {
    LOG_ERROR_NEG("INKMutexUnlock");
  } else
    INKDebug(NEG_DEBUG_TAG, "Neg Test INKMutexUnlock passed\n");
  if (INKHandleMLocRelease(NULL, INK_NULL_MLOC, NULL)) {
    LOG_ERROR_NEG("INKHandleMLocRelease");
  } else
    INKDebug(NEG_DEBUG_TAG, "Neg Test INKHandleMLocRelease 1 passed\n");
  fake_mbuffer = INKMBufferCreate();
  if (INKHandleMLocRelease(fake_mbuffer, INK_NULL_MLOC, NULL)) {
    LOG_ERROR_NEG("INKHandleMLocRelease");
  } else
    INKDebug(NEG_DEBUG_TAG, "Neg Test INKHandleMLocRelease 1 passed\n");
  if (INKHttpTxnReenable(NULL, 0) != INK_ERROR) {
    LOG_ERROR_NEG("INKHttpTxnReenable");
  } else
    INKDebug(NEG_DEBUG_TAG, "Neg Test INKHttpTxnReenable passed\n");
#endif

  return 0;
}

/********************************************************
   Plugin Continuation handler:
   The plugin continuation will be called back by every 
   HTTP transaction when it reach INK_HTTP_TXN_START_HOOK
********************************************************/
static int
plugin_cont_handler(INKCont contp, INKEvent event, void *edata)
{
  LOG_SET_FUNCTION_NAME("process_plugin");

  INKHttpTxn txnp = (INKHttpTxn) edata;

  switch (event) {
  case INK_EVENT_HTTP_TXN_START:
    handle_txn_start(txnp);
    break;

  default:
    INKAssert(!"Unexpected Event");
    break;
  }

  if (INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE) == INK_ERROR) {
    LOG_ERROR("INKHttpTxnReenable");
  }

  return 0;
}

/********************************************************
   Parse the 2 eventual arguments passed to the plugin, 
   else use the defaults.
   Here, there is no need to grab the HOSTNAME_LOCK, this 
   code should be executes before any HTTP state machine 
   is created. 
   Register globally INK_HTTP_TXN_START_HOOK
********************************************************/
void
INKPluginInit(int argc, const char *argv[])
{
  LOG_SET_FUNCTION_NAME("INKPluginInit");

  /* Plugin continuation */
  INKCont contp, fake_contp;
  ContData *fakeData;

  /* Create hostname lock */
  HOSTNAME_LOCK = INKMutexCreate();

  /* Initialize global variables hostname and hostname length  */
  /* No need to grab the lock here  */
  HOSTNAME = INKstrdup("tsdev.inktomi.com");
  HOSTNAME_LENGTH = strlen(HOSTNAME) + 1;

  /* Parse the eventual 2 plugin arguments */
  if (argc < 3) {
    INKDebug(DEBUG_TAG, "Usage: lookup.so hostname hostname_length");
    printf("[lookup_plugin] Usage: lookup.so hostname hostname_length\n");
    printf("[lookup_plugin] Wrong arguments. Using default\n");
  } else {
    HOSTNAME = INKstrdup(argv[1]);
    INKDebug(DEBUG_TAG, "using hostname %s", HOSTNAME);
    printf("[lookup_plugin] using hostname %s\n", HOSTNAME);

    if (!isnan(atoi(argv[2]))) {
      HOSTNAME_LENGTH = atoi(argv[2]);
      INKDebug(DEBUG_TAG, "using hostname length %d", HOSTNAME_LENGTH);
      printf("[lookup_plugin] using hostname length %d\n", HOSTNAME_LENGTH);
    } else {
      printf("[lookup_plugin] Wrong argument for hostname length");
      printf("Using default hostname length %d\n", HOSTNAME_LENGTH);
    }
  }

/* Negative test for INKContCreate, INKHttpHookAdd, INKContDataGet/Set, INKContDestroy */
#ifdef DEBUG
  if (INKHttpHookAdd(-1, NULL) != INK_ERROR) {
    LOG_ERROR_NEG("INKHttpHookAdd");
  } else
    INKDebug(NEG_DEBUG_TAG, "Neg Test INKHttpHookAdd 1 passed\n");
  if (INKHttpHookAdd(INK_HTTP_TXN_START_HOOK, NULL) != INK_ERROR) {
    LOG_ERROR_NEG("INKHttpHookAdd");
  } else
    INKDebug(NEG_DEBUG_TAG, "Neg Test INKHttpHookAdd 2 passed\n");
  fake_contp = INKContCreate(fake_handler2, INKMutexCreate());
  if (INKHttpHookAdd(-1, fake_contp) != INK_ERROR) {
    LOG_ERROR_NEG("INKHttpHookAdd");
  } else
    INKDebug(NEG_DEBUG_TAG, "Neg Test INKHttpHookAdd 3 passed\n");
  if (INKContDataGet(NULL) != INK_ERROR_PTR) {
    LOG_ERROR_NEG("INKContDataGet");
  } else
    INKDebug(NEG_DEBUG_TAG, "Neg Test INKContDataGet passed\n");
  if (INKContDataSet(NULL, NULL) != INK_ERROR) {
    LOG_ERROR_NEG("INKContDataSet");
  } else
    INKDebug(NEG_DEBUG_TAG, "Neg Test INKContDataSet 1 passed\n");
  fakeData = (ContData *) INKmalloc(sizeof(ContData));
  fakeData->called_cache = 0;
  fakeData->cache_lookup_status = -1;
  fakeData->client_port = 0;
  fakeData->ip_address = 0;
  fakeData->txnp = NULL;
  if (INKContDataSet(NULL, fakeData) != INK_ERROR) {
    LOG_ERROR_NEG("INKContDataSet");
  } else
    INKDebug(NEG_DEBUG_TAG, "Neg Test INKContDataSet 3 passed\n");
  if (INKContDestroy(NULL) != INK_ERROR) {
    LOG_ERROR_NEG("INKContDestroy");
  } else
    INKDebug(NEG_DEBUG_TAG, "Neg Test INKContDestroy passed\n");
#endif

  if ((contp = INKContCreate(plugin_cont_handler, NULL)) == INK_ERROR_PTR) {
    LOG_ERROR("INKContCreate");
  } else {
    if (INKHttpHookAdd(INK_HTTP_TXN_START_HOOK, contp) == INK_ERROR) {
      LOG_ERROR("INKHttpHookAdd");
    }
  }
}
