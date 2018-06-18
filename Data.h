#pragma once

#include "ts/ts.h"

#include "HttpHeader.h"
#include "Stage.h"

#include <netinet/in.h>
#include <utility>

struct Data
{
  Data(Data const &) = delete;
  Data & operator=(Data const &) = delete;

  int64_t m_blocksize;
  sockaddr_storage m_client_ip;
/*
  TSMBuffer m_url_buffer;
  TSMLoc m_url_loc;
*/

  std::pair<int64_t, int64_t> m_range_begend;

  int64_t m_blocknum; //!< block number to work on, -1 bad/stop

  TSHttpParser m_http_parser;

  bool m_server_res_header_parsed;
  bool m_client_header_sent;

  Stage m_upstream;
  Stage m_dnstream;

  explicit
  Data
    ( int64_t const blocksize
    )
    : m_blocksize(blocksize)
    , m_client_ip()
/*
    , m_url_buffer(nullptr)
    , m_url_loc(nullptr)
*/
    , m_range_begend(-1, -1)
    , m_blocknum(-1)
    , m_http_parser(nullptr)
    , m_server_res_header_parsed(false)
    , m_client_header_sent(false)
  { }

  ~Data
    ()
  {
/*
    if (nullptr != m_url_loc) {
      TSHandleMLocRelease(m_url_buffer, TS_NULL_MLOC, m_url_loc);
    }
    if (nullptr != m_url_buffer) {
      TSMBufferDestroy(m_url_buffer);
    }
*/
    if (nullptr != m_http_parser) {
      TSHttpParserDestroy(m_http_parser);
    }
  }

  TSHttpParser
  httpParser
    ()
  {
    if (nullptr == m_http_parser) {
      m_http_parser = TSHttpParserCreate();
    }

    return m_http_parser;
  }
};
