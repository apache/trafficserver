#pragma once

/**
 An ATS Http header exists in a marshall buffer at a given location.
 Unfortunately how that marshall buffer is created and how that
 location is determined depends on where those buffers came from.

 A TSHttpTxn manages the buffer itself and creates a location which
 has to be managed.

 A TSHttpParsed populates a created buffer that has had TSHttpHdrCreate
 run against it which creates a location against it.  End users
 need to manage the created buffer, the location and invoke
 TSHttpHdrDestroy.
*/

#include "ts/ts.h"

#include <string>
#include <utility>

static char const * const SLICER_MIME_FIELD_INFO = "X-Slicer-Info";

/**
  Designed to be a cheap throwaway struct which allows a
  consumer to make various calls to manipulate headers.
*/
struct HttpHeader
{
  TSMBuffer const m_buffer;
  TSMLoc const m_lochdr;

  explicit
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

  // TS_HTTP_TYPE_UNKNOWN, TS_HTTP_TYPE_REQUEST, TS_HTTP_TYPE_RESPONSE
  TSHttpType
  type
    () const;

  // response status code
  TSHttpStatus
  status
    () const;

  // response status code
  bool
  setStatus
    ( TSHttpStatus const newstatus
    );

  typedef char const *(*CharPtrGetFunc)(TSMBuffer, TSMLoc, int*);

  // request method TS_HTTP_METHOD_*
  char const *
  method
    ( int * const len=nullptr
    ) const
  {
    return getCharPtr(TSHttpHdrMethodGet, len);
  }

  // host
  char const *
  host
    ( int * const len
    ) const
  {
    return getCharPtr(TSHttpHdrHostGet, len);
  }

  // response reason
  char const *
  reason
    ( int * const len
    ) const
  {
    return getCharPtr(TSHttpHdrReasonGet, len);
  }

  bool
  setReason
    ( char const * const valstr
    , int const vallen
    );

  bool
  hasKey
    ( char const * const key
    , int const keylen
    ) const;

  // returns false if header invalid or something went wrong with removal.
  bool
  removeKey
    ( char const * const key
    , int const keylen
    );

  bool
  valueForKey
    ( char const * const keystr
    , int const keylen
    , char * const valstr // <-- return string value
    , int * const vallen // <-- pass in capacity, returns len of string
    , int const index = -1 // sets all values
    ) const;

/*
  bool
  valueForKeyLast
    ( char const * const keystr
    , int const keylen
    , char * const valstr // <-- return string value
    , int * const vallen // <-- pass in capacity, returns len of string
    , int const index = -1 // sets all values
    ) const;
*/

  /**
    Sets or adds a key/value
  */
  bool
  setKeyVal
    ( char const * const key
    , int const keylen
    , char const * const val
    , int const vallen
    , int const index = -1 // sets all values
    );

  /** dump header into provided char buffer
   */
  std::string
  toString
    () const;

private:

  /**
    To be used with
    TSHttpHdrMethodGet
    TSHttpHdrHostGet
    TSHttpHdrReasonGet
   */
  char const *
  getCharPtr
    ( CharPtrGetFunc func
    , int * const len
    ) const;
};

struct TxnHdrMgr
{
  TxnHdrMgr(TxnHdrMgr const &) = delete;
  TxnHdrMgr & operator=(TxnHdrMgr const &) = delete;

  TSMBuffer m_buffer;
  TSMLoc m_lochdr;

  TxnHdrMgr()
    : m_buffer(nullptr)
    , m_lochdr(nullptr)
  { }

  ~TxnHdrMgr()
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

  bool
  isValid
    () const
  {
    return nullptr != m_lochdr;
  }
};

struct HdrMgr
{
  HdrMgr(HdrMgr const &) = delete;
  HdrMgr & operator=(HdrMgr const &) = delete;

  TSMBuffer m_buffer;
  TSMLoc m_lochdr;

  HdrMgr()
    : m_buffer(nullptr)
    , m_lochdr(nullptr)
  { }

  ~HdrMgr
    ()
  {
    if (nullptr != m_lochdr)
    {
      TSHttpHdrDestroy(m_buffer, m_lochdr);
      TSHandleMLocRelease(m_buffer, TS_NULL_MLOC, m_lochdr);
    }
    if (nullptr != m_buffer)
    {
      TSMBufferDestroy(m_buffer);
    }
  }

  void
  resetHeader
    ()
  {
    if (nullptr != m_lochdr)
    {
      TSHttpHdrDestroy(m_buffer, m_lochdr);
      TSHandleMLocRelease(m_buffer, TS_NULL_MLOC, m_lochdr);
      m_lochdr = nullptr;
    }
  }

  typedef TSParseResult(*HeaderParseFunc)
    (TSHttpParser, TSMBuffer, TSMLoc, char const **, char const *);

  /** Clear/create the parser before calling this and don't
   use the parser on another header until done with this one.
   use one of the following:
     TSHttpHdrParseReq
     TSHttpHdrParseResp
    Call this multiple times if necessary.
  */
  TSParseResult
  populateFrom
    ( TSHttpParser const http_parser
    , TSIOBufferReader const reader
    , HeaderParseFunc const parsefunc
    );


/*
  //! returns false if buffers already allocated or clone fails
  bool
  cloneFrom
    ( TSMBuffer buffersrc
    , TSMLoc locsrc
    );
*/

  bool
  isValid
    () const
  {
    return nullptr != m_lochdr;
  }
};
