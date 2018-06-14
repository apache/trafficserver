#pragma once

#include "ts/ts.h"

#include <netinet/in.h>
#include <utility>

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
    , int64_t const bytesin=INT64_MAX
    )
  {
TSAssert(nullptr != vc);
    if (nullptr == m_iobuf)
    {
      m_iobuf = TSIOBufferCreate();
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
    , int64_t const bytesout=INT64_MAX
    )
  {
TSAssert(nullptr != vc);
    if (nullptr == m_iobuf)
    {
      m_iobuf = TSIOBufferCreate();
      m_reader = TSIOBufferReaderAlloc(m_iobuf);
    }
    else
    {
      drainReader();
    }
    m_vio = TSVConnWrite(vc, contp, m_reader, bytesout);
    return nullptr != m_vio;
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

  TSVConn m_vc { nullptr };
  Channel m_read;
  Channel m_write;

  void
  setupConnection
    ( TSVConn vc
    )
  {
    m_vc = vc;
    m_read.m_vio = nullptr;
    m_write.m_vio = nullptr;
  }

  void
  setupVioRead
    ( TSCont contp
    )
  {
    m_read.setForRead(m_vc, contp);
  }

  void
  setupVioWrite
    ( TSCont contp
    )
  {
    m_write.setForWrite(m_vc, contp);
  }

  bool
  isValid
    () const
  {
    return nullptr != m_vc && m_read.isValid() && m_write.isValid() ;
  }
};

struct Data
{
  Data(Data const &) = delete;
  Data & operator=(Data const &) = delete;

  int64_t m_blocksize;
  std::pair<int64_t, int64_t> m_range_begend;

  int64_t m_blocknum; //!< block number to work on, -1 bad/stop

  TSHttpParser m_http_parser;

  TSIOBuffer m_client_req_header; // request header as read
  TSIOBuffer m_client_res_header; // response header as generated

  bool m_server_res_header_parsed;
  bool m_client_header_sent;

  sockaddr_storage m_client_ip;
  Stage m_upstream;
  Stage m_dnstream;

  explicit
  Data
    ( int64_t const blocksize
    )
    : m_blocksize(blocksize)
    , m_range_begend(-1, -1)
    , m_blocknum(-1)
    , m_http_parser(nullptr)
    , m_client_req_header(nullptr)
    , m_client_res_header(nullptr)
    , m_server_res_header_parsed(false)
    , m_client_header_sent(false)
  { }

  ~Data
    ()
  {
    if (nullptr != m_http_parser) {
      TSHttpParserDestroy(m_http_parser);
    }
    if (nullptr != m_client_req_header) {
      TSIOBufferDestroy(m_client_req_header);
    }
    if (nullptr != m_client_res_header) {
      TSIOBufferDestroy(m_client_res_header);
    }
  }

  TSHttpParser
  httpParser
    ()
  {
    if (nullptr == m_http_parser) {
      m_http_parser = TSHttpParserCreate();
    } else {
      TSHttpParserClear(m_http_parser);
    }

    return m_http_parser;
  }
};
