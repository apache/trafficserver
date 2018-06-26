#pragma once

#include "ts/ts.h"

struct Channel
{
  TSVIO m_vio { nullptr };
  TSIOBuffer m_iobuf { nullptr };
  TSIOBufferReader m_reader { nullptr };

  ~Channel
    ()
  {
    if (nullptr != m_reader) { TSIOBufferReaderFree(m_reader); }
    if (nullptr != m_iobuf) { TSIOBufferDestroy(m_iobuf); }
  }

  void
  drainReader
    ()
  {
TSAssert(nullptr != m_reader);
    int64_t const bytes_avail = TSIOBufferReaderAvail(m_reader);
    TSIOBufferReaderConsume(m_reader, bytes_avail);
  }

  bool
  setForRead
    ( TSVConn vc
    , TSCont contp
    , int64_t const bytesin//=INT64_MAX
    )
  {
TSAssert(nullptr != vc);
    if (nullptr == m_iobuf)
    {
      m_iobuf = TSIOBufferCreate();
//      TSIOBufferWaterMarkSet(m_iobuf, 1024 * 128); // doesn't work
      m_reader = TSIOBufferReaderAlloc(m_iobuf);
    }
    else
    {
      drainReader();
    }
    m_vio = TSVConnRead(vc, contp, m_iobuf, bytesin);
    return nullptr != m_vio;
  }

  bool
  setForWrite
    ( TSVConn vc
    , TSCont contp
    , int64_t const bytesout//=INT64_MAX
    )
  {
TSAssert(nullptr != vc);
    if (nullptr == m_iobuf)
    {
      m_iobuf = TSIOBufferCreate();
//      TSIOBufferWaterMarkSet(m_iobuf, 1024 * 128); // doesn't work
      m_reader = TSIOBufferReaderAlloc(m_iobuf);
    }
    else
    {
      drainReader();
    }
    m_vio = TSVConnWrite(vc, contp, m_reader, bytesout);
    return nullptr != m_vio;
  }

  void
  close
    ()
  {
    if (nullptr != m_reader)
    {
      TSIOBufferReaderFree(m_reader);
      m_reader = nullptr;
    }
    if (nullptr != m_iobuf)
    {
      TSIOBufferDestroy(m_iobuf);
      m_iobuf = nullptr;
    }
  }

  bool
  isValid
    () const
  {
    return nullptr != m_iobuf
      && nullptr != m_reader
      && nullptr != m_vio ;
  }
};

struct Stage // upstream or downstream (server or client)
{
  Stage(Stage const &) = delete;
  Stage & operator=(Stage const &) = delete;

  Stage() { }

  ~Stage() { if (nullptr != m_vc) { TSVConnClose(m_vc); } }

  TSVConn m_vc { nullptr };
  Channel m_read;
  Channel m_write;

  void
  setupConnection
    ( TSVConn vc
    )
  {
    if (nullptr != m_vc)
    {
      TSVConnClose(m_vc);
    }
    m_vc = vc;
    m_read.m_vio = nullptr;
    m_write.m_vio = nullptr;
  }

  void
  setupVioRead
    ( TSCont contp
    , int64_t const bytesin=INT64_MAX
    )
  {
    m_read.setForRead(m_vc, contp, bytesin);
  }

  void
  setupVioWrite
    ( TSCont contp
    , int64_t const bytesout=INT64_MAX
    )
  {
    m_write.setForWrite(m_vc, contp, bytesout);
  }

  void
  close
    ()
  {
    if (nullptr != m_vc)
    {
      TSVConnClose(m_vc);
      m_vc = nullptr;
    }

    m_read.close();
    m_write.close();
  }

  bool
  isValid
    () const
  {
    return nullptr != m_vc && m_read.isValid() && m_write.isValid() ;
  }
};
