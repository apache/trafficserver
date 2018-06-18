#pragma once

#include "ts/ts.h"

/**
  represents value parsed from a Content-Range reponse header field.
  */
struct ContentRange
{
  int64_t m_begin;
  int64_t m_end; // half open
  int64_t m_length; // full content length

  ContentRange () : m_begin(-1), m_end(-1), m_length(-1) { }

  explicit
  ContentRange
    ( int64_t const begin
    , int64_t const end
    , int64_t const len
    )
    : m_begin(begin)
    , m_end(end)
    , m_length(len)
  { }

  bool
  isValid
    () const
  {
    return 0 <= m_begin && m_begin < m_end && m_end < m_length ;
  }

  // null terminated input string, closed range
  bool
  fromStringClosed
    ( char const * const valstr
    );

  // returns null terminated with given length
  bool
  toStringClosed
    ( char * const rangestr
    , int * const rangelen
    ) const;

  int64_t
  rangeSize
    () const
  {
    return m_end - m_begin;
  }
};
