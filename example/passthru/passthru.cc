/** @passthru.cc
 *
 *  Example protocol plugin.
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

/*
 * Passthru plugin.
 *
 * This plugin demonstrates:
 *
 *  - Using TSMgmtStringCreate() to add custom records into records.config.
 *  - Listening on a custom socket with TSPortDescriptorAccept().
 *  - Using TSHttpConnectWithPluginId() and the VConn API to proxy HTTP traffic.
 */

#include <ts/ts.h>
#include <cinttypes>
#include <cstring>

#define PLUGIN_NAME "passthru"

#define PassthruSessionDebug(sp, fmt, ...)                 \
  do {                                                     \
    TSDebug(PLUGIN_NAME, "sp=%p " fmt, sp, ##__VA_ARGS__); \
  } while (0)

static int PassthruSessionEvent(TSCont cont, TSEvent event, void *edata);

union EventArgument {
  void *edata;
  TSVConn vconn;
  TSVIO vio;

  EventArgument(void *_p) : edata(_p) {}
};

struct PassthruIO {
  TSVIO vio               = nullptr;
  TSIOBuffer iobuf        = nullptr;
  TSIOBufferReader reader = nullptr;

  PassthruIO() {}
  ~PassthruIO() { clear(); }
  void
  clear()
  {
    if (this->reader) {
      TSIOBufferReaderFree(this->reader);
    }

    if (this->iobuf) {
      TSIOBufferDestroy(this->iobuf);
    }

    this->reader = nullptr;
    this->iobuf  = nullptr;
    this->vio    = nullptr;
  }

  // Start a read operation.
  void
  read(TSVConn vconn, TSCont contp)
  {
    TSReleaseAssert(this->vio == nullptr);

    this->iobuf  = TSIOBufferCreate();
    this->reader = TSIOBufferReaderAlloc(this->iobuf);
    this->vio    = TSVConnRead(vconn, contp, this->iobuf, INT64_MAX);
  }

  // Start a write operation.
  void
  write(TSVConn vconn, TSCont contp)
  {
    TSReleaseAssert(this->vio == nullptr);

    this->iobuf  = TSIOBufferCreate();
    this->reader = TSIOBufferReaderAlloc(this->iobuf);
    this->vio    = TSVConnWrite(vconn, contp, this->reader, INT64_MAX);
  }

  // Transfer data from this IO object to the target IO object. We use
  // TSIOBufferCopy to move the data without actually duplicating it.
  int64_t
  transfer_to(PassthruIO &to)
  {
    int64_t consumed = 0;
    int64_t avail    = TSIOBufferReaderAvail(this->reader);

    if (avail) {
      consumed = TSIOBufferCopy(to.iobuf, this->reader, avail, 0 /* offset */);
      TSIOBufferReaderConsume(this->reader, consumed);
    }

    return consumed;
  }

  // noncopyable
  PassthruIO(const PassthruIO &) = delete;
  PassthruIO &operator=(const PassthruIO &) = delete;
};

struct PassthruSession {
  // VC session to the client.
  struct {
    TSVConn vconn;
    PassthruIO readio;
    PassthruIO writeio;
  } client;

  // VC session to Traffic Server via TSHttpConnect.
  struct {
    TSVConn vconn;
    PassthruIO readio;
    PassthruIO writeio;
  } server;

  TSCont contp;

  PassthruSession() : contp(TSContCreate(PassthruSessionEvent, TSMutexCreate()))
  {
    this->client.vconn = this->server.vconn = nullptr;
    TSContDataSet(this->contp, this);
  }

  ~PassthruSession()
  {
    if (this->server.vconn) {
      TSVConnClose(this->server.vconn);
    }

    if (this->client.vconn) {
      TSVConnClose(this->client.vconn);
    }

    TSContDataSet(this->contp, nullptr);
    TSContDestroy(this->contp);

    PassthruSessionDebug(this, "destroyed session");
  }

  // noncopyable
  PassthruSession(const PassthruSession &) = delete;
  PassthruSession &operator=(const PassthruSession &) = delete;
};

static bool
PassthruSessionIsFinished(PassthruSession *sp)
{
  int64_t avail = TSIOBufferReaderAvail(sp->client.writeio.reader);

  // We should shut down the session if we don't have a server vconn
  // (either it was never started, or it was closed), and we have drained
  // the client write buffer.
  if (sp->server.vconn == nullptr && avail == 0) {
    return true;
  }

  PassthruSessionDebug(sp, "continuing session with %" PRId64 " buffered client bytes", avail);
  return false;
}

static int
PassthruSessionEvent(TSCont cont, TSEvent event, void *edata)
{
  EventArgument arg(edata);
  PassthruSession *sp = static_cast<PassthruSession *>(TSContDataGet(cont));

  PassthruSessionDebug(sp, "session event on vconn=%p event=%d (%s)", TSVIOVConnGet(arg.vio), event, TSHttpEventNameLookup(event));

  if (event == TS_EVENT_VCONN_READ_READY) {
    // On the first read, wire up the internal transfer to the server.
    if (sp->server.vconn == nullptr) {
      sp->server.vconn = TSHttpConnectWithPluginId(TSNetVConnRemoteAddrGet(sp->client.vconn), PLUGIN_NAME, 0);

      TSReleaseAssert(sp->server.vconn != nullptr);

      // Start the server end of the IO before we write any data.
      sp->server.readio.read(sp->server.vconn, sp->contp);
      sp->server.writeio.write(sp->server.vconn, sp->contp);
    }

    int64_t nbytes;

    nbytes = sp->client.readio.transfer_to(sp->server.writeio);
    PassthruSessionDebug(sp, "proxied %" PRId64 " bytes from client vconn=%p to server vconn=%p", nbytes, sp->client.vconn,
                         sp->server.vconn);
    if (nbytes) {
      TSVIOReenable(sp->client.readio.vio);
      TSVIOReenable(sp->server.writeio.vio);
    }

    nbytes = sp->server.readio.transfer_to(sp->client.writeio);
    PassthruSessionDebug(sp, "proxied %" PRId64 " bytes from server vconn=%p to client vconn=%p", nbytes, sp->server.vconn,
                         sp->client.vconn);
    if (nbytes) {
      TSVIOReenable(sp->server.readio.vio);
      TSVIOReenable(sp->client.writeio.vio);
    }

    if (PassthruSessionIsFinished(sp)) {
      delete sp;
      return TS_EVENT_NONE;
    }

    TSVIOReenable(arg.vio);
    return TS_EVENT_NONE;
  }

  if (event == TS_EVENT_VCONN_WRITE_READY) {
    if (PassthruSessionIsFinished(sp)) {
      delete sp;
      return TS_EVENT_NONE;
    }

    return TS_EVENT_NONE;
  }

  if (event == TS_EVENT_VCONN_EOS) {
    // If we get EOS from the client, just abort everything; we don't
    // care any more.
    if (TSVIOVConnGet(arg.vio) == sp->client.vconn) {
      PassthruSessionDebug(sp, "got EOS from client vconn=%p", sp->client.vconn);

      delete sp;
      return TS_EVENT_NONE;
    }

    // If we get EOS from the server, then we should make sure that we
    // drain any outstanding data before shutting down the client.
    if (TSVIOVConnGet(arg.vio) == sp->server.vconn) {
      PassthruSessionDebug(sp, "EOS from server vconn=%p", sp->server.vconn);

      TSReleaseAssert(sp->client.vconn != nullptr);

      if (TSIOBufferReaderAvail(sp->server.readio.reader) > 0) {
        sp->server.readio.transfer_to(sp->client.writeio);
        TSVIOReenable(sp->client.writeio.vio);
      }

      TSVConnClose(sp->server.vconn);
      sp->server.vconn = nullptr;
      sp->server.readio.clear();
      sp->server.writeio.clear();
    }

    return TS_EVENT_NONE;
  }

  TSError("[%s] unexpected event %s (%d) edata=%p", PLUGIN_NAME, TSHttpEventNameLookup(event), event, arg.edata);

  return TS_EVENT_ERROR;
}

static int
PassthruAccept(TSCont /* cont */, TSEvent event, void *edata)
{
  EventArgument arg(edata);
  PassthruSession *sp = new PassthruSession();

  PassthruSessionDebug(sp, "accepting connection on vconn=%p event=%d", arg.vconn, event);
  TSReleaseAssert(event == TS_EVENT_NET_ACCEPT);

  // Start the client end of the IO. We delay starting the server end until
  // we get the first read from the client end.
  sp->client.vconn = arg.vconn;
  sp->client.readio.read(arg.vconn, sp->contp);
  sp->client.writeio.write(arg.vconn, sp->contp);

  return TS_EVENT_NONE;
}

static TSReturnCode
PassthruListen()
{
  TSMgmtString ports          = nullptr;
  TSPortDescriptor descriptor = nullptr;
  TSCont cont                 = nullptr;

  if (TSMgmtStringGet("config.plugin.passthru.server_ports", &ports) == TS_ERROR) {
    TSError("[%s] missing config.plugin.passthru.server_ports configuration", PLUGIN_NAME);
    return TS_ERROR;
  }

  descriptor = TSPortDescriptorParse(ports);
  if (descriptor == nullptr) {
    TSError("[%s] failed to parse config.plugin.passthru.server_ports", PLUGIN_NAME);
    TSfree(ports);
    return TS_ERROR;
  }

  TSDebug(PLUGIN_NAME, "listening on port '%s'", ports);
  TSfree(ports);

  cont = TSContCreate(PassthruAccept, nullptr);
  return TSPortDescriptorAccept(descriptor, cont);
}

void
TSPluginInit(int /* argc */, const char * /* argv */ [])
{
  TSPluginRegistrationInfo info = {PLUGIN_NAME, "Apache Software Foundation", "dev@trafficserver.apache.org"};

  TSReturnCode status;

  TSMgmtStringCreate(TS_RECORDTYPE_CONFIG, "config.plugin.passthru.server_ports", const_cast<char *>(""),
                     TS_RECORDUPDATE_RESTART_TS, TS_RECORDCHECK_NULL, nullptr /* check_regex */, TS_RECORDACCESS_NULL);

  // Start listening on the configured port.
  status = PassthruListen();
  TSReleaseAssert(status == TS_SUCCESS);

  // Now that succeeded, we can register.
  status = TSPluginRegister(&info);
  TSReleaseAssert(status == TS_SUCCESS);
}

// vim: set sw=2 ts=2 sts=2 et:
