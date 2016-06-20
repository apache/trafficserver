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

#include "UDPAPIClientTest.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>

char sendBuff[] = "I'm Alive.";

FILE *fp;

void
UDPClientTestInit()
{
  TSCont cont;
  unsigned long ip;
  TSMutex readMutexp;

  ip         = inet_addr("209.131.48.79");
  readMutexp = TSMutexCreate();
  cont       = TSContCreate(&UDPClient_handle_callbacks, readMutexp);
  fp         = fopen("UDPAPI.dbg", "a+");
  fprintf(fp, "UDPClient Init called\n");
  fclose(fp);
  INKUDPBind(cont, ip, 9999);
}

int
UDPClient_handle_callbacks(TSCont cont, TSEvent event, void *e)
{
  INKUDPacketQueue packetQueue;
  INKUDPPacket packet;
  TSIOBufferBlock recvBuffBlock;
  unsigned int destIp = inet_addr("209.131.48.79");
  int destPort        = 1813;
  INKUDPConn UDPConn;
  TSIOBufferReader reader;
  TSIOBuffer iobuffer;
  const char *buf;
  int avail, total_len = 0;
  char recvBuff[1024];
  fp = fopen("UDPAPI.dbg", "a+");

  switch (event) {
  case TS_NET_EVENT_DATAGRAM_OPEN:
    UDPConn = (INKUDPConn)e;
    INKUDPRecvFrom(cont, UDPConn);
    INKUDPSendTo(cont, UDPConn, destIp, destPort, sendBuff, strlen(sendBuff));
    fprintf(fp, "sent %s\n.", (const char *)sendBuff);

    break;

  case TS_NET_EVENT_DATAGRAM_READ_READY:
    fprintf(fp, "read ready called\n.");
    packetQueue = (INKUDPacketQueue)e;

    while ((packet = INKUDPPacketGet(packetQueue)) != NULL) {
      recvBuffBlock = INKUDPPacketBufferBlockGet(packet);

      iobuffer = TSIOBufferCreate();
      reader   = TSIOBufferReaderAlloc(iobuffer);
      TSIOBufferAppend(iobuffer, recvBuffBlock);
      buf = TSIOBufferBlockReadStart(recvBuffBlock, reader, &avail);

      if (avail > 0) {
        for (int i = 0; i < avail; i++)
          fprintf(fp, "%c", *(buf + i));

        memcpy((char *)&recvBuff + total_len, buf, avail);
        TSIOBufferReaderConsume(reader, avail);
        total_len += avail;
      }

      /* INKqa10255: we'd free the memory  - jinsheng */
      INKUDPPacketDestroy(packet);
      TSIOBufferReaderFree(reader);
      TSIOBufferDestroy(iobuffer);
    }

    break;

  case TS_NET_EVENT_DATAGRAM_WRITE_COMPLETE:
    break;
  }
  fclose(fp);
  return TS_EVENT_CONTINUE;
}
