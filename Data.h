#pragma once

#include "IPAddress.h"

#include "ts/ts.h"

struct Channel
{
  TSVIO vio;
  TSIOBuffer iobuf;
  TSIOBufferReader reader;

  static
  Channel
  forRead
    ( TSVConn vc
    , TSCont contp
    , int64_t const bytesin=INT64_MAX
    )
  {
    Channel chan;
    TSAssert(nullptr == chan.vio);
    chan.iobuf = TSIOBufferCreate();
    chan.reader = TSIOBufferReaderAlloc(chan.iobuf);
    chan.vio = TSVConnRead(vc, contp, chan.iobuf, bytesin);
    return chan;
  }

  static
  Channel
  forWrite
    ( TSVConn vc
    , TSCont contp
    , int64_t const bytesout=INT64_MAX
    )
  {
    Channel chan;
    TSAssert(nullptr == chan.vio);
    chan.iobuf = TSIOBufferCreate();
    chan.reader = TSIOBufferReaderAlloc(chan.iobuf);
    chan.vio = TSVConnWrite(vc, contp, chan.reader, bytesout);
    return chan;
  }

  Channel
    ()
    : vio(nullptr)
    , iobuf(nullptr)
    , reader(nullptr)
  { }

  ~Channel
    ()
  {
    if (nullptr != reader) { TSIOBufferReaderFree(reader); }
    if (nullptr != iobuf) { TSIOBufferDestroy(iobuf); }
  }

  bool
  isValid
    () const
  {
    return nullptr != vio;
  }
};

struct Stage // upstream or downstream (server or client)
{
  Stage(Stage const &) = delete;
  Stage & operator=(Stage const &) = delete;

  TSVConn vc;
  Channel * read { nullptr };
  Channel * write { nullptr };

  explicit Stage(TSVConn vci) : vc(vci) { }

  ~Stage()
  {
    if (nullptr != read) { delete read; }
    if (nullptr != write) { delete write; }
  }

  void
  setupReader
    ( TSCont contp
    )
  {
    TSAssert(nullptr == read);
    read = new Channel(Channel::forRead(this->vc, contp));
  }

  void
  setupWriter
    ( TSCont contp
    )
  {
    TSAssert(nullptr == write);
    write = new Channel(Channel::forWrite(this->vc, contp));
  }
};

struct Data
{
  Data(Stage const &) = delete;
  Data & operator=(Data const &) = delete;

  IPAddress * ipaddr;
  Stage * upstream { nullptr };
  Stage * dnstream { nullptr };

  Data
    ( sockaddr const * const client_addri
    )
    : ipaddr(new IPAddress(client_addri))
  { }

  ~Data()
  {
    if (nullptr != ipaddr) { delete ipaddr; }
    if (nullptr != upstream) { delete upstream; }
    if (nullptr != dnstream) { delete dnstream; }
  }
};
