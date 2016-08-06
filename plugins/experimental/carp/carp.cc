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

#include <errno.h>

#include <string>
#include <sstream>
#include <stdlib.h> 
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <memory.h>

#include <ts/ts.h>

#include "Common.h"
#include "CarpConfig.h"
#include "CarpConfigPool.h"
#include "CarpHashAlgorithm.h"
#include "UrlComponents.h"

using namespace std;

CarpConfigPool* g_CarpConfigPool = NULL;
int g_carpSelectedHostArgIndex = 0;
TSTextLogObject g_logObject = NULL;

const char *logFileName = "carp";

//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
/*
 check for our carp routed header, dump status if requested
 */
static int
processCarpRoutedHeader(TSHttpTxn txnp, TSMBuffer bufp, TSMLoc hdr_loc)
{
  string value;
  if (getHeader(bufp, hdr_loc, CARP_ROUTED_HEADER, value)) { // if found header
    if (value.compare("1") == 0) { // is loop prevention value
      TSDebug(DEBUG_TAG_HOOK, "Found %s header with loop prevention value, not forwarding again", CARP_ROUTED_HEADER.c_str());
      return 0;
    } else if (value.compare("dump") == 0) { // is dump status request
      TSDebug(DEBUG_TAG_HOOK, "Found %s header with dump request", CARP_ROUTED_HEADER.c_str());
      string status;
      g_CarpConfigPool->getGlobalHashAlgo()->dump(status);
      TSHttpTxnSetHttpRetStatus(txnp, TS_HTTP_STATUS_MULTI_STATUS);
      TSHttpTxnErrorBodySet(txnp, TSstrdup(status.c_str()), status.length(), NULL);
      return -1;
    }
    TSDebug(DEBUG_TAG_HOOK, "Found %s header with unknown value of %s, ignoring", CARP_ROUTED_HEADER.c_str(), value.c_str());
    removeHeader(bufp, hdr_loc, CARP_ROUTED_HEADER);
  }
  return 1; // all OK
}

static bool
checkListForSelf(std::vector<HashNode *> list)
{
  for (size_t k = 0; k < list.size(); k++) {
    if (list[k]->isSelf) return true;
  }
  return false;
}

/**
 bIsPOSTRemap = false --- Hash request and forward to peer
 bIsPOSTRemap = true --- hash request, extract OS sockaddr, insert forwarding header, forward
 */
static int
handleRequestProcessing(TSCont contp, TSEvent event, void *edata, bool bIsPOSTRemap)
{
  TSHttpTxn txnp = (TSHttpTxn) edata;
  TSMBuffer bufp;
  TSMLoc hdr_loc;
  TSMLoc url_loc;

  // get the client request so we can get URL and add header
  if (TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
    TSError("carp couldn't get request headers");
    return -1;
  }

  int method_len;
  const char *method = TSHttpHdrMethodGet(bufp, hdr_loc, &method_len);
  if (NULL == method) {
    TSError("carp couldn't get http method");
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    return -1;
  }
  if (((method_len == TS_HTTP_LEN_DELETE) && (strncasecmp(method, TS_HTTP_METHOD_DELETE, TS_HTTP_LEN_DELETE) == 0)) ||
      ((method_len == TS_HTTP_LEN_PURGE) && (strncasecmp(method, TS_HTTP_METHOD_PURGE, TS_HTTP_LEN_PURGE) == 0))) {
    TSDebug(DEBUG_TAG_HOOK, "Request method is '%s' so not routing request", string(method,method_len).c_str());
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    return 0;
  }

  if (TSHttpHdrUrlGet(bufp, hdr_loc, &url_loc) != TS_SUCCESS) {
    TSError("carp couldn't get url");
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    return -1;
  }
  // if has carp loop prevention header, do not remap
  int iTemp = processCarpRoutedHeader(txnp, bufp, hdr_loc);
  if(iTemp <= 0) { // if dump or do not remap
    // check origin client request's scheme for premap mode
    if (!bIsPOSTRemap) {
      string oriScheme;
      if (!getHeader(bufp, hdr_loc, CARP_PREMAP_SCHEME, oriScheme)) {
        TSDebug(DEBUG_TAG_HOOK, "couldn't get '%s' header", CARP_PREMAP_SCHEME.c_str());
      } else {
        bool isHttps = (oriScheme == TS_URL_SCHEME_HTTPS);
        
        if (isHttps) {
          TSUrlSchemeSet(bufp, url_loc, TS_URL_SCHEME_HTTPS, TS_URL_LEN_HTTPS);
        } else {
          TSUrlSchemeSet(bufp, url_loc, TS_URL_SCHEME_HTTP, TS_URL_LEN_HTTP);  
        }   
        
	removeHeader(bufp, hdr_loc, CARP_STATUS_HEADER);
        removeHeader(bufp, hdr_loc, CARP_ROUTED_HEADER);
        removeHeader(bufp, hdr_loc, CARP_PREMAP_SCHEME.c_str());
        TSDebug(DEBUG_TAG_HOOK, "Set client request's scheme to %s through %s header", 
            isHttps?"https":"http", CARP_PREMAP_SCHEME.c_str());
      }
    } else {
      removeHeader(bufp, hdr_loc, CARP_ROUTED_HEADER);
    }
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    return iTemp;
  }

  UrlComponents reqUrl;
  reqUrl.populate(bufp, url_loc);
  // the url ONLY used to determine the cache owner
  string sUrl;

  if (!bIsPOSTRemap) { // if pre-remap, then host not in URL so get from header
    string sHost;
    if (!getHeader(bufp, hdr_loc, TS_MIME_FIELD_HOST, sHost)) {
      TSDebug(DEBUG_TAG_HOOK, "Could not find host header, ignoring it");
    }
    reqUrl.setHost(sHost);

    //[YTSATS-836] heuristically ignore the scheme and port when calculate cache owner
    UrlComponents normalizedUrl = reqUrl;
    normalizedUrl.setScheme(CARP_SCHEME_FOR_HASH);
    normalizedUrl.setPort(CARP_PORT_FOR_HASH);
    normalizedUrl.construct(sUrl);
  } else {
    reqUrl.construct(sUrl);
  }


  if (g_CarpConfigPool->getGlobalConfig()->hasWhiteList()) {
    string sCarpable;
    if (!getHeader(bufp, hdr_loc, CARPABLE_HEADER, sCarpable)) { // if no carpable header check whitelist
      if (!g_CarpConfigPool->getGlobalConfig()->isWhiteListed(reqUrl.getHost())) { // if white list exists, then host must be present
        TSDebug(DEBUG_TAG_HOOK, "Host '%s' is not whitelisted, not going through carp", reqUrl.getHost().c_str());
        TSHandleMLocRelease(bufp, hdr_loc, url_loc);
        TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
        return 0;
      } else {
        TSDebug(DEBUG_TAG_HOOK, "Found host (%s) whitelisted, routing...",reqUrl.getHost().c_str());
      }
    } else { // found carpable header, make sure it's 1
      if (sCarpable.compare("1") != 0) { // carpable header present but not 0, be strict and do not forward request
        TSDebug(DEBUG_TAG_HOOK, "Carpable (%s) present but value not acceptable (%s)", CARPABLE_HEADER.c_str(), sCarpable.c_str());
        TSHandleMLocRelease(bufp, hdr_loc, url_loc);
        TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
        return 0;
      } else {
        TSDebug(DEBUG_TAG_HOOK, "Found Carpable header, routing...");
      }
    }
  } else { // no whitelist so blacklist could be used
    if (g_CarpConfigPool->getGlobalConfig()->isBlackListed(reqUrl.getHost())) { // if host black listed, do not carp
      TSDebug(DEBUG_TAG_HOOK, "Host '%s' is blacklisted, not going through carp", reqUrl.getHost().c_str());
      TSHandleMLocRelease(bufp, hdr_loc, url_loc);
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
      return 0;
    }
  }
  
  TSDebug(DEBUG_TAG_HOOK, "URL to hash with=%s", sUrl.c_str());

  // get nodeList and select node to forward to
  std::vector<HashNode *> nodeList = g_CarpConfigPool->getGlobalHashAlgo()->getRemapProxyList(sUrl);
  bool bAddHeaderResult=false;
  bool bAddForwardedResult = false;
  HashNode* node = NULL;
  bool bIsOwner = false;

  if (nodeList.size() == 0) { // no hosts available to forward to
    TSDebug(DEBUG_TAG_HOOK, "no hosts available to forward to, will handle locally");
    goto done;
  } else {
    node = nodeList[0];
  }

  bIsOwner = checkListForSelf(nodeList);
  for (size_t k = 0; k < nodeList.size(); k++) {
    TSDebug(DEBUG_TAG_HOOK, "nodeList host %d name is %s", static_cast<int>(k), nodeList[k]->name.c_str());
  }

  //handle forwarding
  TSDebug(DEBUG_TAG_HOOK, "forwarding to '%s' (isSelf=%d)", node->name.c_str(), node->isSelf);
  if (!node->isSelf) { // carp does not forward if we choose ourself
    node->carpForward();
    TSDebug(DEBUG_TAG_HOOK, "carp forwarded to %s.", node->name.c_str());
    // insert carp loop prevention header
    bAddHeaderResult = addHeader(bufp, hdr_loc, CARP_ROUTED_HEADER, string("1"));
    if (!bAddHeaderResult) {
      TSError("Carp, error inserting '%s' header", CARP_ROUTED_HEADER.c_str());
    }
    bAddForwardedResult = addHeader(bufp, hdr_loc, CARP_STATUS_HEADER, string(CARP_FORWARDED));
    if (!bAddForwardedResult) {
      TSError("Carp, error inserting '%s' header", CARP_STATUS_HEADER.c_str());
    }

    if (bIsPOSTRemap) { // if post remap, get remapped/OS Server Addr and add as header
      const struct sockaddr* sa = TSHttpTxnServerAddrGet(txnp);
      //        const struct sockaddr* sa = TSHttpTxnNextHopAddrGet(txnp);
      if (sa) { // sanity check
        struct sockaddr_storage ss;
        memcpy(static_cast<void *> (&ss), sa, sizeof (sockaddr_storage));
        if ((reinterpret_cast<sockaddr_in *> (&ss))->sin_port == 0) { // set port from client request URL
          (reinterpret_cast<sockaddr_in *> (&ss))->sin_port = htons(reqUrl.getPort());
        }

        string sTemp;
        getStringFromSockaddr(reinterpret_cast<const sockaddr *> (&ss), sTemp);
        TSDebug(DEBUG_TAG_HOOK, "Inserting forward header with sockaddr:%s", sTemp.c_str());
        string sSockaddr;
        sSockaddr.reserve(32 + sizeof (sockaddr_storage) * 2);
        for (unsigned int i = 0; i<sizeof (sockaddr_storage); i++) {
          char val[8];
          sprintf(val, "%02X", reinterpret_cast<const unsigned char *> (&ss)[i]);
          sSockaddr += val;
        }
        sSockaddr += "/" + reqUrl.getScheme();
        // insert carp forwarding header
        bool bAddFwdHeaderResult = addHeader(bufp, hdr_loc, CARP_FORWARD_HEADER, sSockaddr);
        if (!bAddFwdHeaderResult) {
          TSError("Carp, error inserting '%s' header", CARP_FORWARD_HEADER.c_str());
        }
      }
    } else { // for premap mode
      string sScheme = reqUrl.getScheme();

      if (!addHeader(bufp, hdr_loc, CARP_PREMAP_SCHEME, sScheme)) { 
        TSError("Carp, error inserting '%s' header in premap mode", CARP_PREMAP_SCHEME.c_str());  
      } else {
        TSDebug(DEBUG_TAG_HOOK, "Insert client request scheme %s in premap mode", sScheme.c_str());
      }
    }
    // set origin server/destination
    //      TSHttpTxnConfigIntSet(txnp, TS_CONFIG_HTTP_SHARE_SERVER_SESSIONS, 0); // disable conn sharing due to bug when used with TSHttpTxnServerAddrSet
    if (TSHttpTxnServerAddrSet(txnp, reinterpret_cast<const struct sockaddr *> (&node->forwardAddr)) != TS_SUCCESS) {
      TSDebug(DEBUG_TAG_HOOK, "Error calling TSHttpTxnServerAddrSet");
    } else {
      // set scheme appropriately based on destination
      TSDebug(DEBUG_TAG_HOOK, "Setting scheme to '%s'", node->getSchemeString());
      TSUrlSchemeSet(bufp, url_loc, node->getSchemeString(), -1);
      if (!bIsPOSTRemap) { // since we are forwarding, do not remap request and do not cache result
        TSSkipRemappingSet(txnp, true);
      }
      TSHttpTxnArgSet(txnp, g_carpSelectedHostArgIndex, static_cast<void *> (node));
      if (!bIsOwner) {
        TSHttpTxnServerRespNoStoreSet(txnp, 1); // do not cache the response
      } else {
        TSHttpTxnServerRespNoStoreSet(txnp, 0); // we are replicate owner, cache response
      }
    }
  } else {
    node->carpNoForward();
    TSDebug(DEBUG_TAG_HOOK, "carp forwarded to self.");
  }

done :
  // done w/buffers, release them
  TSHandleMLocRelease(bufp, hdr_loc, url_loc);
  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
  return 0;
}

/**
 * Convert ASCII hex digit to value of hex digit
 * @param ch
 * @return 
 */
static unsigned char
getValueOfHex(unsigned char ch)
{
  ch -= '0';
  if(ch > 9) ch -= 7; // 'A' - ':' = 7
  return ch;
}

/**
 Process request pre-remap while running post-remap mode
 if forwarding header present and rules met, disable remap processing, forward to OS
 */
static int
handleForwardRequestProcessing(TSCont contp, TSEvent event, void *edata)
{
  // we only want to do anything here if CARP_ROUTED_HEADER present
  // and CARP_FORWARD_HEADER is present
  // and incoming request is from port getAllowedForwardPort()
  TSHttpTxn txnp = (TSHttpTxn) edata;
  TSMBuffer bufp;
  TSMLoc hdr_loc;

  // get the client request 
  if (TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
    TSError("carp couldn't get request headers");
    return -1;
  }

  // if has carp loop prevention header, do not remap
  int iTemp = processCarpRoutedHeader(txnp, bufp, hdr_loc);
  if(iTemp == -1 || iTemp == 1) { // is dump request or routed header not present
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    return (iTemp == 1) ? 0 : iTemp; // return 0 if routed header not present
  }
    
  // check incoming port number first
  const struct sockaddr_in* sa = reinterpret_cast<const sockaddr_in *>(TSHttpTxnIncomingAddrGet(txnp));
  if(!sa) {
    TSDebug(DEBUG_TAG_HOOK, "TSHttpTxnIncomingAddrGet() returned NULL");
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    return 0;
  }
       
  int iPort = ntohs(sa->sin_port);
  if(iPort != g_CarpConfigPool->getGlobalConfig()->getAllowedForwardPort()) {
    TSDebug(DEBUG_TAG_HOOK, "Allowed forward port does not match.  Incoming request on %d, but configured for %d.",iPort, g_CarpConfigPool->getGlobalConfig()->getAllowedForwardPort());
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    return 0;
  }
  
  TSDebug(DEBUG_TAG_HOOK, "Incoming requests port number validated (%d), continuing", iPort);

  string value;
  if (getHeader(bufp, hdr_loc, CARP_ROUTED_HEADER, value)) { // if found header make sure is valid loop prevention header
    if (value.compare("1") != 0) { // is not expected value, leave
      TSDebug(DEBUG_TAG_HOOK, "Carp routed header not loop prevention value");
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
      return 0;
    }
  } else { // routed header not there, get out of here
    TSDebug(DEBUG_TAG_HOOK, "Carp routed header not present");
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    return 0;
  }

  if (!getHeader(bufp, hdr_loc, CARP_FORWARD_HEADER, value)) { // forward header not found, get out of here
    TSDebug(DEBUG_TAG_HOOK, "Carp forward header not present");
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    return 0;
  }

  bool bIsHttps = false;
  size_t hexStringEnd = value.find("/");
  if(hexStringEnd == string::npos) {
    hexStringEnd = value.length();
  } else { // header contains '/http' or '/https'
    bIsHttps = (value.find("https",hexStringEnd) != string::npos) ? true : false;
  }

  struct sockaddr_storage sas;
  char *ptr = reinterpret_cast<char *> (&sas);
  for (unsigned int i = 0; i < hexStringEnd; i += 2) {
    unsigned char ch = (getValueOfHex(value[i]) << 4) + getValueOfHex(value[i + 1]);
    ptr[i >> 1] = ch;
  }
  
  string sTemp;
  getStringFromSockaddr(reinterpret_cast<const sockaddr *> (&sas), sTemp);
  TSDebug(DEBUG_TAG_HOOK, "Extracted sockaddr from forward header:%s", sTemp.c_str());
  
//  TSHttpTxnConfigIntSet(txnp, TS_CONFIG_HTTP_SHARE_SERVER_SESSIONS, 0); // disable conn sharing due to bug when used with TSHttpTxnServerAddrSet
  if (TSHttpTxnServerAddrSet(txnp, reinterpret_cast<const struct sockaddr *> (&sas)) != TS_SUCCESS) {
    TSDebug(DEBUG_TAG_HOOK, "Error calling TSHttpTxnServerAddrSet");
    return -1;
  }

  // set scheme for connection to OS (ats will use the scheme of the carp client that connected if we don't do this)
  // solves issue when inbound client connection scheme does not match carp peer connection scheme
  TSMLoc url_loc;
  if (TSHttpHdrUrlGet(bufp, hdr_loc, &url_loc) != TS_SUCCESS) {
    TSError("carp couldn't get url");
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    return -1;
  }

  if(bIsHttps) {
    TSUrlSchemeSet(bufp, url_loc, TS_URL_SCHEME_HTTPS, TS_URL_LEN_HTTPS);
  } else {
    TSUrlSchemeSet(bufp, url_loc, TS_URL_SCHEME_HTTP, TS_URL_LEN_HTTP);
  }
  
  // request is ready for OS, do not remap
  TSSkipRemappingSet(txnp, true);
  removeHeader(bufp, hdr_loc, CARP_FORWARD_HEADER); // remove our header
  removeHeader(bufp, hdr_loc, CARP_STATUS_HEADER);
  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
      TSDebug(DEBUG_TAG_HOOK, "Carp life should be good");
  return 0;
}

/**
 Process request post-remap while running post-remap mode
 hash request, extract OS sockaddr, insert forwarding header, forward
 */
static int
handlePostRemapRequestProcessing(TSCont contp, TSEvent event, void *edata)
{
  return handleRequestProcessing(contp, event, edata, true);
}

/**
    This function is called to process origin responses to detect bad ppers 
 */
static int
handleResponseProcessing(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp = (TSHttpTxn) edata;

  HashNode *node = static_cast<HashNode *> (TSHttpTxnArgGet(txnp, g_carpSelectedHostArgIndex));
  if (node) { // if was forwarded by carp
    bool bDown = false;
    TSServerState conState = TSHttpTxnServerStateGet(txnp);
    TSDebug(DEBUG_TAG_HOOK, "TSHttpTxnServerStateGet(txnp)=%d",conState);
    
    // the following list of "bad" server states taken from HttpTransact::is_response_valid(State* s, HTTPHdr* incoming_response)
    switch(conState) {
    case TS_SRVSTATE_CONNECTION_ERROR :
      TSDebug(DEBUG_TAG_HOOK, "Connection error");
      bDown = true;
      break;
    case TS_SRVSTATE_CONNECTION_CLOSED :
      TSDebug(DEBUG_TAG_HOOK, "Connection closed");
      bDown = true;
      break;
    case TS_SRVSTATE_ACTIVE_TIMEOUT :
      TSDebug(DEBUG_TAG_HOOK, "Active timeout");
      bDown = true;
      break;
    case TS_SRVSTATE_INACTIVE_TIMEOUT :
      TSDebug(DEBUG_TAG_HOOK, "Inactive timeout");
      bDown = true;
      break;
    case TS_SRVSTATE_OPEN_RAW_ERROR :
      TSDebug(DEBUG_TAG_HOOK, "Open raw error");
      bDown = true;
      break;
    case TS_SRVSTATE_PARSE_ERROR :
      TSDebug(DEBUG_TAG_HOOK, "Parse error");
      bDown = true;
      break;
    case TS_SRVSTATE_CONGEST_CONTROL_CONGESTED_ON_F :
      TSDebug(DEBUG_TAG_HOOK, "Congest control congested on F");
      bDown = true;
      break;
    case TS_SRVSTATE_CONGEST_CONTROL_CONGESTED_ON_M :
      TSDebug(DEBUG_TAG_HOOK, "Congest control congested on M");
      bDown = true;
      break;
    default: // assume all other cases ok
      break;
    }
    if(bDown) {
      g_CarpConfigPool->getGlobalHashAlgo()->setStatus(node->name,node->listenPort,false,time(NULL),0);
      TSDebug(DEBUG_TAG_HOOK, "marking %s as down",node->name.c_str());
    }
  } else {
    TSMLoc hdr_loc;
    TSMBuffer bufp;

    if (TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
      TSError("carp couldn't get request headers");
      return -1;
    }

    string value;
    if (getHeader(bufp, hdr_loc, CARP_STATUS_HEADER, value)) { //if found header
      if((value.compare(CARP_FORWARDED) == 0)) {
        removeHeader(bufp, hdr_loc, CARP_STATUS_HEADER);
      }
    }

    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);

    TSDebug(DEBUG_TAG_HOOK, "request not routed by carp");
  }
  return 0;
}

/**
    This function is called on every request (when global is enabled) 
 */
int
carpPluginHook(TSCont contp, TSEvent event, void *edata)
{
  int iReturn = 0;
  TSHttpTxn txnp = (TSHttpTxn) edata;

  // handle the event for correct state
  switch (event) {

  case TS_EVENT_HTTP_READ_REQUEST_HDR:
    TSDebug(DEBUG_TAG_HOOK, "event TS_EVENT_HTTP_READ_REQUEST_HDR");
    if (g_CarpConfigPool->getGlobalConfig()->getMode() == CarpConfig::PRE) { // pre-remap mode
      iReturn = handleRequestProcessing(contp, event, edata, false);
    } else { // post remap mode
      // check for forward header
      iReturn = handleForwardRequestProcessing(contp, event, edata);
    }
    break;
    
  case TS_EVENT_HTTP_OS_DNS: // only used in POST-REMAP mode
    TSDebug(DEBUG_TAG_HOOK, "event TS_EVENT_HTTP_OS_DNS");
    iReturn = handlePostRemapRequestProcessing(contp, event, edata);
    break;
    
 case TS_EVENT_HTTP_SEND_RESPONSE_HDR:
    TSDebug(DEBUG_TAG_HOOK, "event TS_EVENT_HTTP_SEND_RESPONSE_HDR");
    iReturn = handleResponseProcessing(contp, event, edata);
    break;
 
    // do nothing for any of the other states
  default:
    TSDebug(DEBUG_TAG_HOOK, "event default..why here? event=%d", event);
    break;
  }

  if (iReturn == -1) { // indicate error
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_ERROR);
  } else { // no error
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  }

  return iReturn;
}


int
configReloadHandler(TSCont cont, TSEvent event, void *edata) {
  struct CarpConfigAndHash *pdata =
      (struct CarpConfigAndHash *) TSContDataGet(cont);
  struct stat s;
  string path = pdata->_configPath;
  CarpConfigAndHash *cch = pdata;
  TSDebug(DEBUG_TAG_HEALTH, "try to check if file %s was modified",
      path.c_str());
  if (stat(path.c_str(), &s) >= 0) {
    TSDebug(DEBUG_TAG_HEALTH, "current m time %ld last time %ld", s.st_mtime,
        pdata->_lastLoad);
    if (s.st_mtime > pdata->_lastLoad) {
      TSDebug(DEBUG_TAG_HEALTH, "config file %s have been modified",
          path.c_str());
      pdata->_lastLoad = s.st_mtime;

      cch = g_CarpConfigPool->processConfigFile(pdata->_configPath, true);

      if (NULL == cch) {
        TSError("Failed to reload config file '%s'", path.c_str());
        TSDebug(DEBUG_TAG_HEALTH, "Failed to reload config file '%s'",
            path.c_str());
        TSAssert(0);
        return 0; // need to return in the case we are unit testing
      } else {
        TSContDataSet(cont, (void *) cch);
        TSDebug(DEBUG_TAG_HEALTH, "Succeed reload the config from %s",
            path.c_str());
      }
    }
  } else {
    TSDebug(DEBUG_TAG_HEALTH, "read config file  %s failed", path.c_str());
  }

  TSDebug(DEBUG_TAG_HEALTH, "The frequency of the reload is %d",
      cch->_config->getConfigCheckFreq());

  TSContSchedule(cont, cch->_config->getConfigCheckFreq() * 1000, TS_THREAD_POOL_TASK);

  return 1;
}


/**
    Entry point for the plugin. Responsibilities include:
      - Read plugin configuration file.
      - Register hooks with HTTP states.
      - Save static information for use upon each request.
      - Initialize stat variables.
 
 Hooks:
  TS_HTTP_READ_REQUEST_HDR_HOOK - In default mode, hash request and forward.
                                  In post-remap mode, check for routing header and forward.
  TS_HTTP_OS_DNS_HOOK           - In default mode. not used.
                                  In post-remap mode, hash request, extract OS sockaddr 
                                    and insert as header, and forward.
  TS_HTTP_SEND_RESPONSE_HDR_HOOK - [both modes] detect errors connecting to a peer to 'down' it faster
 */
void
TSPluginInit(int argc /* ATS_UNUSED */, const char * argv[] /* ATS_UNUSED */) 
{
  TSDebug(DEBUG_TAG_INIT, "Initializing global plugin with %d arguments", argc);
  for (int i = 0; i < argc; i++) {
    TSDebug(DEBUG_TAG_INIT, "argv[%d]=%s", i, argv[i]);
  }

  if (NULL == g_CarpConfigPool) {
    g_CarpConfigPool = new CarpConfigPool;
    if (NULL == g_CarpConfigPool) {
      TSError("Unable to allocate memory");
      TSDebug(DEBUG_TAG_INIT, "Unable to allocate memory");
      TSAssert(g_CarpConfigPool);
      return; // need to return in the case we are unit testing
    }
  }
  
  // expecting 2 args, [0]=carp.so [1]=configFileName
  if (2 != argc) {
    TSError("Usage: %s <config-file>.  Exactly 2 arguments required, %d arguments given in plugin.config\n", argv[0], argc);
    TSDebug(DEBUG_TAG_INIT, "Usage: %s <config-file>.  Exactly 2 arguments required, %d arguments given in plugin.config\n", argv[0], argc);
    TSAssert(2 != argc);
    return; // need to return in the case we are unit testing
  }

  if(TSHttpArgIndexReserve("carp", "host that was selected to forward to", &g_carpSelectedHostArgIndex) != TS_SUCCESS) {
    TSError("Failed to reserve an argument index");
    TSDebug(DEBUG_TAG_INIT, "Failed to reserve an argument index");
    TSAssert(0);
    return; // need to return in the case we are unit testing
  }

  string filename = string(argv[1]);
  struct stat s;

  TSDebug(DEBUG_TAG_INIT, "Try to access the config file %s ", filename.c_str());
  if (stat(filename.c_str(), &s) < 0) {
    // now try with default path
    string newFilename = string(TSConfigDirGet()) + "/" + filename;
    TSDebug(DEBUG_TAG_INIT, "failed, now trying to get the config file stat in this path: %s", newFilename.c_str());

    if (stat(newFilename.c_str(), &s) < 0) {
      TSError("The access to config files %s and %s failed", filename.c_str(), newFilename.c_str());
      TSDebug(DEBUG_TAG_INIT, "The access to config files %s and %s failed", filename.c_str(), newFilename.c_str());
      TSAssert(0);
      return;
    } else {
      filename = newFilename;
    }
  }

  CarpConfigAndHash* cch = g_CarpConfigPool->processConfigFile(filename.c_str(),true);
  cch->_lastLoad = s.st_mtime;

  if(NULL == cch) {
    TSError("Failed to process config file '%s'", argv[1]);
    TSDebug(DEBUG_TAG_INIT, "Failed to process config file '%s'", argv[1]);
    TSAssert(0);
    return; // need to return in the case we are unit testing
  }

  TSCont configContp;
  configContp = TSContCreate(configReloadHandler, TSMutexCreate());
  TSContDataSet(configContp, (void*)cch);
  TSContSchedule(configContp, 0, TS_THREAD_POOL_TASK);
  // hook in to ATS
  TSCont contp;

  contp = TSContCreate(carpPluginHook, NULL);

  if (contp == NULL) {
    TSError("carp could not create continuation");
    TSDebug(DEBUG_TAG_INIT, "carp could not create continuation");
    TSAssert(contp);
    return; // need to return in the case we are unit testing
  }
  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, contp); 
  TSHttpHookAdd(TS_HTTP_SEND_RESPONSE_HDR_HOOK, contp);

  // also hook POST remap if running in post remap mode  
  if (g_CarpConfigPool->getGlobalConfig()->getMode() == CarpConfig::POST) {
    TSHttpHookAdd(TS_HTTP_OS_DNS_HOOK, contp);
  }
}
