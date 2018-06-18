#include "ContentRange.h"

#include <cinttypes>
#include <cstdio>

static char const * const format
  = "bytes %" PRId64 "-%" PRId64 "/%" PRId64;

bool
ContentRange :: fromStringClosed
  ( char const * const valstr
  )
{
//TSAssert(nullptr != valstr);
  int const fields = sscanf(valstr, format, &m_begin, &m_end, &m_length);

  if (3 == fields && m_begin <= m_end)
  {
    m_end += 1;
    return true;
  }
  else
  {
    m_begin = m_end = m_length = -1;
  }

  return false;
}

bool
ContentRange :: toStringClosed
  ( char * const rangestr
  , int * const rangelen
  ) const
{
//TSAssert(nullptr != rangestr);
//TSAssert(nullptr != rangelen);

  if (! isValid())
  {
    *rangelen = 0;
    return false;
  }

  int const lenin = *rangelen;
  *rangelen = snprintf
    ( rangestr, lenin
    , format
    , m_begin, m_end - 1, m_length );

  return (0 < *rangelen && *rangelen < lenin);
}
