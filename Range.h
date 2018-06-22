#pragma once

#include "ts/ts.h"

/**
  represents a value parsed from a Range request header field.
  Range is converted from a closed range into a half open.
 */

struct Range
{

public:

  int64_t m_beg;
  int64_t m_end;// half open

  Range() : m_beg(-1), m_end(-1) { }

  explicit
  Range
    ( int64_t const begin
    , int64_t const end
    )
    : m_beg(begin)
    , m_end(end)
  { }

  bool
  isValid
    () const;

  /** parse a from a closed request range into a half open range
   */
  bool
  fromStringClosed
    ( char const * const rangestr
    );

  /** parse a from a closed request range into a half open range
   */
  bool
  toStringClosed
    ( char * const rangestr
    , int * const rangelen
    ) const;

  /** block number of first range block
   */
  int64_t
  firstBlockFor
    ( int64_t const blockbytes
    ) const;

  /** block intersection
   */
  Range
  intersectedWith
    ( Range const & other
    ) const;

  /** is the given block inside held range?
   */
  bool
  blockIsInside
    ( int64_t const blocksize
    , int64_t const blocknum
    ) const;

  /** number of skip bytes for the given block
   */
  int64_t
  skipBytesForBlock
    ( int64_t const blocksize
    , int64_t const blocknum
    ) const;

};
