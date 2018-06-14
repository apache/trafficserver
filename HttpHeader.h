#pragma once

#include "ts/ts.h"

#include <utility>

struct HttpHeader
{
  TSMBuffer m_buffer;
  TSMLoc m_lochdr;

  HttpHeader
    ( TSMBuffer buffer
    , TSMLoc lochdr
    )
    : m_buffer(buffer)
    , m_lochdr(lochdr)
  { }

  bool
  isValid
    () const 
  {
    return nullptr != m_buffer && nullptr != m_lochdr;
  }

  // returns nullptr or TS_HTTP_METHOD_*
  char const *
  method
    () const;

  bool
  skipMe
    () const;

  //! parse the first range of interest (for now)
  std::pair<int64_t, int64_t>
  firstRange
    () const;

  // hack for hard coded content type check (refactor)
  int64_t
  contentBytes
    () const;

  //! set skip me header for self Http Connect
  bool
  setSkipMe
    ();
};

class TxnHeader
{
  TxnHeader(TxnHeader const &) = delete;
  TxnHeader & operator=(TxnHeader const &) = delete;

  TSMBuffer m_buffer;
  TSMLoc m_lochdr;

public:

  TxnHeader()
    : m_buffer(nullptr)
    , m_lochdr(nullptr)
  { }

  ~TxnHeader()
  {
    if (nullptr != m_lochdr) {
      TSHandleMLocRelease(m_buffer, TS_NULL_MLOC, m_lochdr);
    }
  }

  typedef TSReturnCode(*HeaderGetFunc)(TSHttpTxn, TSMBuffer *, TSMLoc*);
  /** use one of the following:
    TSHttpTxnClientReqGet
    TSHttpTxnClientRespGet
    TSHttpTxnServerReqGet
    TSHttpTxnServerRespGet
    TSHttpTxnCachedReqGet
    TSHttpTxnCachedRespGet
  */

  bool
  populateFrom
    ( TSHttpTxn const & txnp
    , HeaderGetFunc const & func
    )
  {
    return TS_SUCCESS == func(txnp, &m_buffer, &m_lochdr);
  }

  HttpHeader
  header
    () const
  {
TSAssert(nullptr != m_buffer && nullptr != m_lochdr);
    return HttpHeader(m_buffer, m_lochdr);
  }
};

class ParseHeader
{
  ParseHeader(ParseHeader const &) = delete;
  ParseHeader & operator=(ParseHeader const &) = delete;

public:

  TSMBuffer m_buffer;
  TSMLoc m_lochdr;

  ParseHeader()
    : m_buffer(nullptr)
    , m_lochdr(nullptr)
  { }

  ~ParseHeader
    ()
  {
    if (nullptr != m_buffer && nullptr != m_lochdr) {
      TSHttpHdrDestroy(m_buffer, m_lochdr);
      TSHandleMLocRelease(m_buffer, TS_NULL_MLOC, m_lochdr);
      TSMBufferDestroy(m_buffer);
    }
  }

  typedef TSParseResult(*HeaderParseFunc)
    (TSHttpParser, TSMBuffer, TSMLoc, char const **, char const *);
  /** Clear/create the parser before calling this and don't
   use the parser on another header until done with this one.
   use one of the following:
     TSHttpHdrParseReq
     TSHttpHdrParseResp
    Call this multiple times if necessary
  */
  TSParseResult
  populateFrom
    ( TSHttpParser const http_parser
    , TSIOBufferReader const reader
    , HeaderParseFunc const parsefunc
    );

  HttpHeader
  header
    () const
  {
TSAssert(nullptr != m_buffer && nullptr != m_lochdr);
    return HttpHeader(m_buffer, m_lochdr);
  }
};
