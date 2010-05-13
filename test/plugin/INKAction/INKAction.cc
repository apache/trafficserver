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

/* INKAction
 *
 * TODO send and receive data on the connection
*/

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

/* in milliseconds */
#define TIMEOUT_VAL (30000)


/* Set in INKPluginInit */
typedef struct clientInfo
{
  char clientBuff[BUFSIZ];
  int port;
} clientInfo_t;

/* Assumes that type-o-serve.pl (or some other client accepting
 * connections) is running on a client machine
*/
static int
handle_INKAction(INKCont contp, INKEvent event, void *eData)
{
  INKHttpTxn txnp = (INKHttpTxn *) eData;
  char *clientName = NULL;
  unsigned int clientAddr = 0;
  int err = 0;
  char *ipAddrp = NULL;
  clientInfo_t *clientInfop = NULL;
  struct in_addr *inAddrp = NULL;
  INKAction inkAction;
  struct hostent *hostEntp;

  switch (event) {
  case INK_EVENT_IMMEDIATE:
    /* event scheduled at plugIn-init
     */
    clientInfop = (clientInfo_t *) INKContDataGet(contp);
    if (!clientInfop) {
      INKDebug("INKAction", "INKContDataGet returned NULL ptr");
      return 1;
    }

    /* wrs: Unix network programming
     * Get hostname/port name: ip str :
     * unsigned int  inet_addr(ip str)
     */
    INKDebug("INKAction", "gethostbyname( %s )", clientInfop->clientBuff);
    hostEntp = gethostbyname(clientInfop->clientBuff);
    if (!hostEntp) {
      INKDebug("INKAction", "failed: gethostbyname( %s )", clientInfop->clientBuff);
      return ++err;
    }
    inAddrp = (struct in_addr *) *hostEntp->h_addr_list;

    /* host addr in inAddrp to inet std dot notation */
    clientName = inet_ntoa(*inAddrp);
    /* string clientName in inet std dot not. to integer value */
    clientAddr = inet_addr(clientName);
    INKDebug("INKAction",
             "INKNetConnect(contp, client=(%s/%d), port=(%d))",
             clientName, htonl(clientAddr), ntohl(clientInfop->port));

    /* We should get NET_CONNECT or NET_CONNECT_FAILED before
     * this schedule timeout event
     */
    INKContSchedule(contp, TIMEOUT_VAL);

    inkAction = INKNetConnect(contp, htonl(clientAddr), ntohl(clientInfop->port));
    if (!INKActionDone(inkAction)) {
      INKDebug("INKAction", "INKNetConnect: not called back yet, action not done");
      INKContDataSet(contp, (void *) inkAction);
    } else {
      INKDebug("INKAction", "INKNetConnect: plug-in has been called ");
    }
    break;

  case INK_EVENT_TIMEOUT:
    inkAction = INKContDataGet(contp);

    /* INK_EVENT_NET_CONNECT_FAILED or
     * INK_EVENT_NET_CONNECT have been received
     * then action should be considered  done
     */
    if (inkAction && !INKActionDone(inkAction)) {
      INKActionCancel(inkAction);
      err++;                    /* timed out w/no event */
      /* no error */
      INKDebug("INKAction", "INKAction: INK_EVENT_TIMEOUT action not done");
    } else {
      /* no error */
      INKDebug("INKAction", "INKAction: INK_EVENT_TIMEOUT");
    }
    break;

  case INK_EVENT_NET_CONNECT_FAILED:
    INKDebug("INKAction", "INKNetConnect: INK_EVENT_NET_CONNECT_FAILED ***** ");
    INKContDataSet(contp, NULL);  /* TODO determine what does this do */
    break;

  case INK_EVENT_NET_CONNECT:
    INKDebug("INKAction", "INKNetConnect: INK_EVENT_NET_CONNECT");
    INKContDataSet(contp, NULL);  /* TODO determine what does this do */
    break;

  default:
    INKDebug("INKAction", "handle_INKAction: undefined event ");
    break;
  }
  return err;
}



/* Usage:
 * INKAction.so clientName clientPort
*/
void
INKPluginInit(int argc, const char *argv[])
{
  int err = 0;
  int length = 0, argCnt = 0;
  clientInfo_t *clientp = NULL;
  INKCont contp;
  struct hostent *hostEntp = NULL;
  char *hostName = NULL;
  int port = 0;

  ink_assert(argc == 3);
  clientp = (clientInfo_t *) INKmalloc(sizeof(clientInfo_t));
  ink_assert(clientp != NULL);

  contp = INKContCreate(handle_INKAction, INKMutexCreate());

  hostEntp = gethostbyname(argv[++argCnt]);
  if (!hostEntp) {
    INKDebug("INKAction", "Failed: gethostbyname returned null pointer");
    return;
  }
  strncpy(clientp->clientBuff, hostEntp->h_name, strlen(hostEntp->h_name));

  clientp->port = atoi(argv[++argCnt]);

  INKContDataSet(contp, (void *) clientp);

  INKContSchedule(contp, 0);
  /* INKHttpHookAdd(INK_HTTP_SSN_START_HOOK, contp); */
}
