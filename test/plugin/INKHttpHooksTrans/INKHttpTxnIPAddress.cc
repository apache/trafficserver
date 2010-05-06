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


#include "ts.h"
#include "ink_assert.h"
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>


/* cvrt unsigned int address to dotted decimal address  
 * delete on the otherside, otherwise: don't care
*/
char *
uint2ddip(unsigned int addr)
{
  char *ptr = (char *) INKmalloc(64);
  if (!ptr)
    return NULL;

  unsigned int address = addr;
  unsigned char *ip = (unsigned char *) &address;

  sprintf(ptr, "%d.%d.%d.%d", (unsigned int) ip[0], (unsigned int) ip[1], (unsigned int) ip[2], (unsigned int) ip[3]);

  return ptr;
}


/* Set in INKPluginInit */
typedef struct parentProxyInfo
{
  char parentProxyp[BUFSIZ];
  int parentPort;
} parentProxyInfo_t;

static int
handle_SEND_REQUEST(INKCont contp, INKEvent event, void *eData)
{
  INKHttpTxn txnp = (INKHttpTxn *) eData;
  unsigned int nextHopIP = 0;
  int err = 0;
  char *ipAddrp = NULL;

  /* Origin Server (destination) or Parent IP 
   * TODO use return addr with an actual network library routine (gethostbyaddr) to validate addr
   * TODO tests with an actual parent proxy
   */
  nextHopIP = INKHttpTxnNextHopIPGet(txnp);
  if (!nextHopIP) {
    INKDebug("INKHttpTxnIPAddress", "INKHttpTxnNextHopIPGet failed");
    return ++err;
  }
  ipAddrp = uint2ddip(nextHopIP);
  INKDebug("INKHttpTxnIPAddress", "INKHttpTxnNextHopIPGet passed for %s", ipAddrp ? ipAddrp : "NULL ptr!");
  INKfree(ipAddrp);
  /* TODO validate this IP address, not just an integer value 
   * see below 
   */
  return err;
}


/* Test:
* INKHttpTxnClientReqGet
* INKHttpTxnServerIPGet (specific)
* INKHttpHdrUrlGet
* INKUrlHostGet 
* Test is to use the address returend by INKHttpTxnServerIPGet
* with a standard network interface api and compare that 
* host with the hostname found in the request URL.
*/
static int
handle_OS_DNS(INKCont contp, INKEvent event, void *eData)
{
  unsigned int os_ip;
  INKHttpTxn txnp = (INKHttpTxn *) eData;
  struct in_addr inAddr;
  struct hostent *hostEntp = NULL;
  int err = 0;
  const char *reqURLHost = NULL;
  int hostLen = 0;
  char strTokenp = '.';         /* URLs separated by this token */
  char *domain_os_ip = NULL, *domain_url = NULL;
  INKMBuffer buf;
  unsigned int nextHopIP;
  INKMLoc loc, hdrLoc;
  char *ptr = NULL;

  /* See: handle_SEND_REQUEST(): nextHopIP = INKHttpTxnNextHopIPGet(txnp);
   */
  os_ip = INKHttpTxnServerIPGet(txnp);
  if (os_ip) {
    inAddr.s_addr = os_ip;
    hostEntp = gethostbyaddr((const char *) &inAddr, sizeof(struct in_addr), AF_INET);
    if (!hostEntp) {
      ptr = uint2ddip(os_ip);
      /* failure */
      INKDebug("INKHttpTxnIPAddress", "INKHttpTxnServerIPGet: gethostbyaddr failed for %s", ptr ? ptr : "NULL ptr!");
      INKfree(ptr);
      return ++err;
    }
  } else {
    /* failure */
    INKDebug("INKHttpTxnIPAddress", "INKHttpTxnServerIPGet: gethostbyaddr no hostname");
    ++err;
  }
  if (!INKHttpTxnClientReqGet(txnp, &buf, &loc)) {
    /* failure */
    INKDebug("INKHttpTxnIPAddress", "INKHttpTxnServerIPGet: INKHttpTxnClientReqGet failed");
    return ++err;               /* ret here, else INKMHandleRelease */
  }

  if ((hdrLoc = INKHttpHdrUrlGet(buf, loc)) == NULL) {
    /* failure */
    INKDebug("INKHttpTxnIPAddress", "INKHttpTxnServerIPGet: INKHttpHdrURLGet failed");
    INKHandleMLocRelease(buf, INK_NULL_MLOC, loc);
    return ++err;               /* ret here, else INKMHandleRelease */
  }

  /* no memory del on reqURLHost */
  reqURLHost = INKUrlHostGet(buf, hdrLoc, &hostLen);
  if (!reqURLHost || !hostLen) {
    /* FAILURE */
    INKDebug("INKHttpTxnIPAddress", "INKHttpTxnServerIPGet: gethostbyaddr no hostname");
    INKHandleMLocRelease(buf, INK_NULL_MLOC, loc);
    return ++err;
  }
  /* compare the domains of the hostname 
   * from gethostbyaddr and INKURLHostGet: e.g.:
   * w1.someGiantSite.com with a request of: www.someGiantSite.com
   */
  else {
    /* compare with domain (!host) from os_ip
     * Not using hostLen
     */
    domain_url = strchr((char *) reqURLHost, (int) strTokenp);
    domain_os_ip = strchr(hostEntp->h_name, (int) strTokenp);

    if (!domain_os_ip || !domain_url) {
      INKDebug("INKHttpTxnIPAddress", "INKHttpTxnServerIPGet: fail: strtok");
      ++err;
    }
    if (strncmp(++domain_os_ip, ++domain_url, BUFSIZ)) {
      INKDebug("INKHttpTxnIPAddress", "INKHttpTxnServerIPGet: fail: domain names %s != %s", domain_os_ip, domain_url);
      ++err;
    }
  }
  INKHandleMLocRelease(buf, INK_NULL_MLOC, loc);
  return err;
}

/* Currently not used.  Interfaces like INKHttpTxnNextHopIPGet 
 * should only be called from SEND_REQUEST, inclusive, forward 
*/
static int
handle_TXN_START(INKCont contp, INKEvent event, void *eData)
{
  return 0;
}

static int
handle_TXN_CLOSE(INKCont contp, INKEvent event, void *eData)
{
  INKMBuffer respBuf;
  INKMLoc respBufLoc;
  INKHttpTxn txnp = (INKHttpTxn) eData;
  char *hostNamep = NULL;
  void *dataPtr = NULL;
  int err = 0, re = 0;
  char **hostname = NULL;
  int hostPort = 0;
  unsigned int os_addr = 0;
  unsigned int clientIP, nextHopIP;
  int incomingPort;
  char *ipAddrp = NULL;

  incomingPort = INKHttpTxnClientIncomingPortGet(txnp);
  if (!incomingPort) {
    INKDebug("INKHttpTxnIPAddress", "INKHttpTxnClientIncomingPortGet failed");
    ++err;                      /* failure */
  }
  /* TODO validate this port, not just an integer value
   * see below 
   */

  /* Client IP for a transaction (not incoming) */
  clientIP = INKHttpTxnClientIPGet(txnp);
  if (!clientIP) {
    INKDebug("INKHttpTxnIPAddress", "INKHttpTxnClientIPGet failed");
    err++;
  }
  /* TODO validate this IP address, not just an integer value
   * see below 
   */

  /* See: handle_SEND_REQUEST(): nextHopIP = INKHttpTxnNextHopIPGet(txnp);
   * 
   * If origin server was contacted, its adress
   * will be returned. Need a cach hit true/false interface ? 
   *
   * Origin Server (destination) or Parent IP 
   * TODO tests with an actual parent proxy
   */
  nextHopIP = INKHttpTxnNextHopIPGet(txnp);
  if (!nextHopIP) {
    /* It is the responsibility of the plug-in to store hit/miss
     * details and resolve this as INKHttpTxnNextHopIPGet failure
     * or cache miss (no o.s. contected). 
     */
    INKDebug("INKHttpTxnIPAddress", "INKHttpTxnNextHopIPGet failed for or cache miss");
    err++;
  }
  /* TODO validate this IP address, not just an integer value 
   * see below 
   */

        /*********************************************************** 
	 * Failure in the following tests will cause remaining tests 
	 * to not execute. 
	*/
  os_addr = INKHttpTxnServerIPGet(txnp);
  if (!os_addr) {
    INKDebug("INKHttpTxnIPAddress", "INKHttpTxnServerIPGet failed");
    return ++err;               /* failure */
  }

  hostname = (char **) INKmalloc(BUFSIZ);
  /* if parent proxy is not set: re is -1
   */
  INKHttpTxnParentProxyGet(txnp, hostname, &hostPort);
  /* TODO value of hostname when parent not set?  */
  if (hostPort == (-1) || *hostname == NULL) {
    INKDebug("INKHttpTxnIPAddress", "INKHttpTxnParentProxyGet failed");
    /* return ++err;    failure */
    /* Allow other test to continue */
  }

  /* 
   * Get the parent/port that were set at plug-in init 
   */
  dataPtr = INKContDataGet(contp);
  if (!dataPtr) {
    INKDebug("INKHttpTxnIPAddress", "INKContDataGet returned NULL pointer, cannot test INKHttpTxnParentProxySet");
    return ++err;
  }

  INKDebug("INKHttpTxnIPAddress",
           "Setting parent proxy to %s:%d",
           ((parentProxyInfo_t *) dataPtr)->parentProxyp, ((parentProxyInfo_t *) dataPtr)->parentPort);

  /* TODO how do we check return value? */
  INKHttpTxnParentProxySet(txnp,
                           ((parentProxyInfo_t *) dataPtr)->parentProxyp, ((parentProxyInfo_t *) dataPtr)->parentPort);

  /* parent proxy was set
   */
  INKHttpTxnParentProxyGet(txnp, hostname, &hostPort);
  if (hostPort == (-1) || *hostname == NULL) {
    INKDebug("INKHttpTxnIPAddress", "INKHttpTxnParentProxyGet failed");
    /* return ++err;    failure */
    /* Allow other test to continue */
  }

  /* Compare */
  if ((strncmp(*hostname,
               ((parentProxyInfo_t *) dataPtr)->parentProxyp, BUFSIZ)) ||
      ((parentProxyInfo_t *) dataPtr)->parentPort != hostPort) {
    INKDebug("INKHttpTxnIPAddress", "INKHttpTxnParentProxySet/Get failed");
    ++err;
  }

  INKfree(hostname);
  INKfree(hostname);
  return err;
}

static int
INKHttpTransaction(INKCont contp, INKEvent event, void *eData)
{
  INKHttpSsn ssnp = (INKHttpSsn) eData;
  INKHttpTxn txnp = (INKHttpTxn) eData;
  int err = 0;
  unsigned int nextHopIP = 0;

  switch (event) {

  case INK_EVENT_HTTP_SSN_START:
    INKHttpSsnHookAdd(ssnp, INK_HTTP_TXN_START_HOOK, contp);
    INKHttpSsnHookAdd(ssnp, INK_HTTP_TXN_CLOSE_HOOK, contp);
    INKHttpSsnHookAdd(ssnp, INK_HTTP_SEND_REQUEST_HDR_HOOK, contp);
    INKHttpSsnHookAdd(ssnp, INK_HTTP_OS_DNS_HOOK, contp);

    INKHttpSsnReenable(ssnp, INK_EVENT_HTTP_CONTINUE);
    break;

  case INK_EVENT_HTTP_OS_DNS:
    handle_OS_DNS(contp, event, eData);
    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
    break;

  case INK_EVENT_HTTP_TXN_START:
    handle_TXN_START(contp, event, eData);
    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
    break;

  case INK_EVENT_HTTP_TXN_CLOSE:
    handle_TXN_CLOSE(contp, event, eData);
    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
    break;

  case INK_EVENT_HTTP_SEND_REQUEST_HDR:
    handle_SEND_REQUEST(contp, event, eData);
    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
    break;

  default:
    break;
  }
  return err;
}

void
INKPluginInit(int argc, const char *argv[])
{
  int err = 0;
  int length = 0, argCnt = 0;
  ink_assert(argc == 3);

  /* Passed in as args to the plug-in  and  does not get deleted
   */
  parentProxyInfo_t *parentInfop = (parentProxyInfo_t *) INKmalloc(sizeof(parentProxyInfo_t));

  if (!parentInfop) {
    INKDebug("INKHttpTxnIPAddress", "INKmalloc(parentProxyInfo_t = [%d]) failed", sizeof(parentProxyInfo_t));
    INKDebug("INKHttpTxnIPAddress", "INKHttpTxnIPAddress failed and did not run");
    return;
  }
  strncpy(parentInfop->parentProxyp, argv[++argCnt], strlen(argv[argCnt]));
  parentInfop->parentPort = atoi(argv[++argCnt]);

  INKCont contp = INKContCreate(INKHttpTransaction, NULL);
  INKContDataSet(contp, (void *) parentInfop);  /* Used in txn */

  /* Never: INKfree(parentInfop); 
   * if you expect to use INKContDataGet
   * not a leak since TS keeps a reference to this heap 
   * space
   * Here's a leak (bad stuff man!):
   * 
   *       ptr = INKmalloc() ;
   *       Init(ptr);
   *       INKConDataSet(contp, ptr);
   *
   * at some other event at a later time
   *
   *        retreivePtr = INKContDataGet(contp);
   *        newPtr = Modify(retrievePtr);
   *        INKConDataSet(contp, newPtr);
   *        INKfree(retrievedPtr);                if not freed, leak
   */

  INKHttpHookAdd(INK_HTTP_SSN_START_HOOK, contp);
}
