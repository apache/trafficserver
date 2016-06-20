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

#include "UDPAPITest.h"
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

char ACK[] = "I got it.";

FILE *fp;

void
UDPTestInit()
{
  TSCont cont;
  TSMutex readMutexp;
  unsigned long ip;

  ip         = inet_addr("209.131.48.79");
  readMutexp = TSMutexCreate();
  cont       = TSContCreate(&handle_callbacks, readMutexp);
  //      INKUDPBind(cont, INADDR_ANY,1813);
  INKUDPBind(cont, ip, 1813);
}

void
printN(const char *start, int length)
{
  int i;
  for (i = 0; i < length; i++)
    fprintf(fp, "%c", *(start + i));
  fprintf(fp, "\n");
}

int
handle_callbacks(TSCont cont, TSEvent event, void *e)
{
  INKUDPacketQueue packetQueue;
  INKUDPPacket packet;
  TSIOBufferBlock recvBuffBlock;
  TSIOBufferReader reader;
  TSIOBuffer iobuffer;
  INKUDPConn UDPConn;
  unsigned int ip;
  int port;
  int *sizep;
  int size;
  char sendBuff[32];
  const char *buf;
  int avail, total_len;
  char recv_buffer[4096];

  fp = fopen("UDPServer.log", "a+");

  switch (event) {
  case TS_NET_EVENT_DATAGRAM_OPEN:
    fprintf(fp, "open event called\n");
    UDPConn = (INKUDPConn)e;
    INKUDPRecvFrom(cont, UDPConn);
    break;

  case TS_NET_EVENT_DATAGRAM_READ_READY:
    fprintf(fp, "read ready event called\n");
    packetQueue = (INKUDPacketQueue)e;
    total_len   = 0;
    while ((packet = INKUDPPacketGet(packetQueue)) != NULL) {
      recvBuffBlock = INKUDPPacketBufferBlockGet(packet);
      iobuffer      = TSIOBufferCreate();
      reader        = TSIOBufferReaderAlloc(iobuffer);
      TSIOBufferAppend(iobuffer, recvBuffBlock);
      buf = TSIOBufferBlockReadStart(recvBuffBlock, reader, &avail);

      if (avail > 0) {
        fprintf(fp, "Received message is\n");
        printN(buf, avail);
        fprintf(fp, "message length = %i\n", avail);
        memcpy((char *)&recv_buffer + total_len, buf, avail);
        TSIOBufferReaderConsume(reader, avail);
        total_len += avail;
      }

      ip   = INKUDPPacketFromAddressGet(packet);
      port = INKUDPPacketFromPortGet(packet);
      fprintf(fp, "port = %d\n", port);

      UDPConn = (INKUDPConn)INKUDPPacketConnGet(packet);
      INKUDPSendTo(cont, UDPConn, ip, port, ACK, strlen(ACK));

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
