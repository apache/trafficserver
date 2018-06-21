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

/* // for pristine url coming in
  TSMBuffer m_urlbuffer { nullptr };
  TSMLoc m_urlloc { nullptr };
*/
  char m_hostname[1024];
  int m_hostlen;

  TSHttpStatus m_statustype; // 200 or 206

  bool m_bail; // non 206/200 response

  std::pair<int64_t, int64_t> m_range_begend;
  int64_t m_contentlen;

  int64_t m_blocknum; //!< block number to work on, -1 bad/stop
  int64_t m_skipbytes; //!< number of bytes to skip in this block

  int64_t m_bytestosend; //!< header + content bytes to send
  int64_t m_bytessent; //!< number of content bytes sent

  bool m_server_block_header_parsed;
  bool m_server_first_header_parsed;
  bool m_client_header_sent;

  Stage m_upstream;
  Stage m_dnstream;

  HdrMgr m_req_hdrmgr; //!< manager for server request
  HdrMgr m_resp_hdrmgr; //!< manager for client response

  TSHttpParser m_http_parser { nullptr }; //!< cached for reuse

  explicit
  Data
    ( int64_t const blocksize
    )
    : m_blocksize(blocksize)
    , m_client_ip()
/*
    , m_urlbuffer(nullptr)
    , m_urlloc(nullptr)
*/
    , m_hostlen(0)
    , m_statustype(TS_HTTP_STATUS_NONE)
    , m_bail(false)
    , m_range_begend(-1, -1)
    , m_contentlen(-1)
    , m_blocknum(-1)
    , m_skipbytes(0)
    , m_bytestosend(0)
    , m_bytessent(0)
    , m_server_block_header_parsed(false)
    , m_server_first_header_parsed(false)
    , m_client_header_sent(false)
    , m_http_parser(nullptr)
  {
    m_hostname[0] = '\0';
  }

  ~Data
    ()
  {
/*
    if (nullptr != m_urlloc && nullptr != m_urlbuffer)
    {
      TSHandleMLocRelease(m_urlbuffer, TS_NULL_MLOC, m_urlloc);
    }
    if (nullptr != m_urlbuffer)
    {
      TSMBufferDestroy(m_urlbuffer);
    }
*/
    if (nullptr != m_http_parser)
    {
      TSHttpParserDestroy(m_http_parser);
    }
  }
};
