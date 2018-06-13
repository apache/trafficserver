#pragma once

#include "ts/ts.h"

#include <utility>

class HttpTxnHeader
{
  HttpTxnHeader(HttpTxnHeader const &) = delete;
  HttpTxnHeader & operator=(HttpTxnHeader const &) = delete;

public:

  typedef TSReturnCode(*HeaderGetFunc)(TSHttpTxn, TSMBuffer *, TSMLoc*);

  // default null constructor
  HttpTxnHeader
    ();

  explicit
  HttpTxnHeader
    ( TSHttpTxn const & txnp
    , HeaderGetFunc const & func
      // TSHttpTxnClientReqGet
      // TSHttpTxnClientRespGet
      // TSHttpTxnServerReqGet
      // TSHttpTxnServerRespGet
      // TSHttpTxnCachedReqGet
      // TSHttpTxnCachedRespGet
    );

  ~HttpTxnHeader
    ();

  bool
  isValid
    () const;

  bool
  isMethodGet
    () const;

  //! parse the first range of interest (for now)
  std::pair<int64_t, int64_t>
  firstRange
    () const;

  //! set skip me header for self Http Connect
  bool
  setSkipMe
    ();

  bool
  skipMe
    () const;

  // hack for hard coded content type check (refactor)
  int64_t
  contentBytes
    () const;

private:  // methods

  bool
  allocBuffer
    () const;

private: // data

  TSHttpTxn m_txnp;
  TSMBuffer m_buffer; //!< buffer from header get func
  TSMLoc m_lochdr; //!< location of header from get func

};
