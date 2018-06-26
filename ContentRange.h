#pragma once

#include "ts/ts.h"

/**
  represents value parsed from a blocked Content-Range reponse header field.
  Range is converted from closed range into a half open range for.
 */
struct ContentRange
{
  int64_t m_beg;
  int64_t m_end; // half open
  int64_t m_length; // full content length

  ContentRange () : m_beg(-1), m_end(-1), m_length(-1) { }

  explicit
  ContentRange
    ( int64_t const begin
    , int64_t const end
    , int64_t const len
    )
    : m_beg(begin)
    , m_end(end)
    , m_length(len)
  { }

  bool
  isValid
    () const
  {
    return 0 <= m_beg && m_beg < m_end && m_end <= m_length ;
  }

  /** parsed from a Content-Range field
   */
  bool
  fromStringClosed
    ( char const * const valstr
    );

  /** usable for Content-Range field
   */
  bool
  toStringClosed
    ( char * const rangestr
    , int * const rangelen
    ) const;

  int64_t
  rangeSize
    () const
  {
    return m_end - m_beg;
  }
};
