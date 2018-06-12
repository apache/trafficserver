#pragma once

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
    , int64t const bytesout=INT64_MAX
    )
  {
    Channel chan;
    TSAssert(nullptr == chan.vio);
    chan.iobuf = TSIOBufferCreate();
    chan.reader = TSIOBufferReaderAlloc(iobuf);
    chan.vio = TSVConnRead(vc, contp, iobuf, bytesin);
    return chan;
  }

  static
  Channel
  forWrite
    ( TSVConn vc
    , TSCont contp
    , int64t const bytesout=INT64_MAX
    )
  {
    Channel chan;
    TSAssert(nullptr == chan.vio);
    chan.iobuf = TSIOBufferCreate();
    chan.reader = TSIOBufferReaderAlloc(iobuf);
    chan.vio = TSVConnWrite(vc, contp, iobuf, bytesout);
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
    if (nullptr != reader)
    {
      TSIOBufferReaderFree(reader);
    }
    if (nullptr != iobuf)
    {
      TSIOBufferDestroy(iobuf);
    }
  }

  bool
  isValid
    () const
  {
    return nullptr != vio;
  }
};
