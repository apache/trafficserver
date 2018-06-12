#pragma once

#include "ts/ts.h"

#include <utility>

class HttpHeader
{
  HttpHeader(HttpHeader const &) = delete;
  HttpHeader & operator=(HttpHeader const &) = delete;

public:

  typedef TSReturnCode(*HeaderGetFunc)(TSHttpTxn, TSMBuffer *, TSMLoc*);

  // default null constructor
  HttpHeader
    ();

  explicit
  HttpHeader
    ( TSHttpTxn const & txnp
    , HeaderGetFunc const & func
      // 
    );

  ~HttpHeader
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

  // hack for hard coded respoinse check (refactor)
  bool
  isStatusOkay
    () const;

  // hack for hard coded content type check (refactor)
  bool
  isContentText
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

  TSHttpTxn txnp;
  TSMBuffer buffer; //!< buffer from header get func
  TSMLoc lochdr; //!< location of header from get func

};
