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

  bool m_passthru; // non 206 response

  std::pair<int64_t, int64_t> m_range_begend;
  int64_t m_contentlen;

  int64_t m_blocknum; //!< block number to work on, -1 bad/stop

  TSHttpParser m_http_parser; //! cached for reuse

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
    , m_passthru(false)
    , m_range_begend(-1, -1)
    , m_contentlen(-1)
    , m_blocknum(-1)
    , m_http_parser(nullptr)
    , m_server_res_header_parsed(false)
    , m_client_header_sent(false)
  { }

  ~Data
    ()
  {
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
