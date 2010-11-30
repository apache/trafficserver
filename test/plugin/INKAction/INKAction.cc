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
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* TSAction
 *
 * TODO send and receive data on the connection
*/

/* cvrt unsigned int address to dotted decimal address
 * delete on the otherside, otherwise: don't care
*/
char *
uint2ddip(unsigned int addr)
{
  char *ptr = (char *) TSmalloc(64);
  if (!ptr)
    return NULL;

  unsigned int address = addr;
  unsigned char *ip = (unsigned char *) &address;

  sprintf(ptr, "%d.%d.%d.%d", (unsigned int) ip[0], (unsigned int) ip[1], (unsigned int) ip[2], (unsigned int) ip[3]);

  return ptr;
}

/* in milliseconds */
#define TIMEOUT_VAL (30000)


/* Set in TSPluginInit */
typedef struct clientInfo
{
  char clientBuff[BUFSIZ];
  int port;
} clientInfo_t;

/* Assumes that type-o-serve.pl (or some other client accepting
 * connections) is running on a client machine
*/
static int
handle_TSAction(TSCont contp, TSEvent event, void *eData)
{
  TSHttpTxn txnp = (TSHttpTxn *) eData;
  char *clientName = NULL;
  unsigned int clientAddr = 0;
  int err = 0;
  char *ipAddrp = NULL;
  clientInfo_t *clientInfop = NULL;
  struct in_addr *inAddrp = NULL;
  TSAction inkAction;
  struct hostent *hostEntp;

  switch (event) {
  case TS_EVENT_IMMEDIATE:
    /* event scheduled at plugIn-init
     */
    clientInfop = (clientInfo_t *) TSContDataGet(contp);
    if (!clientInfop) {
      TSDebug("TSAction", "TSContDataGet returned NULL ptr");
      return 1;
    }

    /* wrs: Unix network programming
     * Get hostname/port name: ip str :
     * unsigned int  inet_addr(ip str)
     */
    TSDebug("TSAction", "gethostbyname( %s )", clientInfop->clientBuff);
    hostEntp = gethostbyname(clientInfop->clientBuff);
    if (!hostEntp) {
      TSDebug("TSAction", "failed: gethostbyname( %s )", clientInfop->clientBuff);
      return ++err;
    }
    inAddrp = (struct in_addr *) *hostEntp->h_addr_list;

    /* host addr in inAddrp to inet std dot notation */
    clientName = inet_ntoa(*inAddrp);
    /* string clientName in inet std dot not. to integer value */
    clientAddr = inet_addr(clientName);
    TSDebug("TSAction",
             "TSNetConnect(contp, client=(%s/%d), port=(%d))",
             clientName, htonl(clientAddr), ntohl(clientInfop->port));

    /* We should get NET_CONNECT or NET_CONNECT_FAILED before
     * this schedule timeout event
     */
    TSContSchedule(contp, TIMEOUT_VAL);

    inkAction = TSNetConnect(contp, htonl(clientAddr), ntohl(clientInfop->port));
    if (!TSActionDone(inkAction)) {
      TSDebug("TSAction", "TSNetConnect: not called back yet, action not done");
      TSContDataSet(contp, (void *) inkAction);
    } else {
      TSDebug("TSAction", "TSNetConnect: plug-in has been called ");
    }
    break;

  case TS_EVENT_TIMEOUT:
    inkAction = TSContDataGet(contp);

    /* TS_EVENT_NET_CONNECT_FAILED or
     * TS_EVENT_NET_CONNECT have been received
     * then action should be considered  done
     */
    if (inkAction && !TSActionDone(inkAction)) {
      TSActionCancel(inkAction);
      err++;                    /* timed out w/no event */
      /* no error */
      TSDebug("TSAction", "TSAction: TS_EVENT_TIMEOUT action not done");
    } else {
      /* no error */
      TSDebug("TSAction", "TSAction: TS_EVENT_TIMEOUT");
    }
    break;

  case TS_EVENT_NET_CONNECT_FAILED:
    TSDebug("TSAction", "TSNetConnect: TS_EVENT_NET_CONNECT_FAILED ***** ");
    TSContDataSet(contp, NULL);  /* TODO determine what does this do */
    break;

  case TS_EVENT_NET_CONNECT:
    TSDebug("TSAction", "TSNetConnect: TS_EVENT_NET_CONNECT");
    TSContDataSet(contp, NULL);  /* TODO determine what does this do */
    break;

  default:
    TSDebug("TSAction", "handle_TSAction: undefined event ");
    break;
  }
  return err;
}



/* Usage:
 * TSAction.so clientName clientPort
*/
void
TSPluginInit(int argc, const char *argv[])
{
  int err = 0;
  int length = 0, argCnt = 0;
  clientInfo_t *clientp = NULL;
  TSCont contp;
  struct hostent *hostEntp = NULL;
  char *hostName = NULL;
  int port = 0;

  ink_assert(argc == 3);
  clientp = (clientInfo_t *) TSmalloc(sizeof(clientInfo_t));
  ink_assert(clientp != NULL);

  contp = TSContCreate(handle_TSAction, TSMutexCreate());

  hostEntp = gethostbyname(argv[++argCnt]);
  if (!hostEntp) {
    TSDebug("TSAction", "Failed: gethostbyname returned null pointer");
    return;
  }
  strncpy(clientp->clientBuff, hostEntp->h_name, strlen(hostEntp->h_name));

  clientp->port = atoi(argv[++argCnt]);

  TSContDataSet(contp, (void *) clientp);

  TSContSchedule(contp, 0);
  /* TSHttpHookAdd(TS_HTTP_SSN_START_HOOK, contp); */
}
