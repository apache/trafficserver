#include "HttpHeader.h"

#include "slice.h"

#include <cassert>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace
{

} // namespace

char const *
HttpHeader :: getCharPtr
  ( CharPtrGetFunc func
  , int * const len
  ) const
{
  char const * res = nullptr;
  if (isValid())
  {
    int reslen = 0;
    res = func(m_buffer, m_lochdr, &reslen);

    if (nullptr != len)
    {
      *len = reslen;
    }
  }

  return res;
}

/*
  char content_range[1024];
  int const content_range_len(snprintf
    ( content_range, 1024
    , "bytes=%" PRId64 "-%" PRId64
    , rangebe.first, rangebe.second - 1 ) );
*/

/*
std::pair<int64_t, int64_t>
HttpHeader :: firstRangeHalfOpen
   () const
{
  std::pair<int64_t, int64_t> range
    (0, std::numeric_limits<int64_t>::max());

  TSMLoc const locfield
    ( TSMimeHdrFieldFind
      ( m_buffer
      , m_lochdr
      , TS_MIME_FIELD_RANGE
      , TS_MIME_LEN_RANGE ) );

  if (nullptr != locfield)
  {
    int value_len = 0;
    char const * const value = TSMimeHdrFieldValueStringGet
        (m_buffer, m_lochdr, locfield, 0, &value_len);

    if (0 < value_len && value_len < 255)
    {
      char rangearr[256];
      memcpy(rangearr, value, value_len);
      rangearr[value_len + 1] = '\0';
      range = parseRange(rangearr);
      range.second += 1;
    }

    TSHandleMLocRelease(m_buffer, m_lochdr, locfield);
  }
  
  return range;
}

*/

bool
HttpHeader :: hasKey
  ( char const * const key
  , int const keylen
  ) const
{
  if (! isValid())
  {
    return false;
  }

  TSMLoc const locfield
    (TSMimeHdrFieldFind(m_buffer, m_lochdr, key, keylen));
  if (nullptr != locfield)
  {
    TSHandleMLocRelease(m_buffer, m_lochdr, locfield);
    return true;
  }

  return false;
}

bool
HttpHeader :: removeKey
  ( char const * const keystr
  , int const keylen
  )
{
  if (! isValid())
  {
    return false;
  }

  bool status = true;

  TSMLoc const locfield = TSMimeHdrFieldFind
    (m_buffer, m_lochdr, keystr, keylen);
  if (nullptr != locfield)
  {
    int const rcode = TSMimeHdrFieldRemove(m_buffer, m_lochdr, locfield);
    status = (TS_SUCCESS == rcode);
    TSHandleMLocRelease(m_buffer, m_lochdr, locfield);
  }

  return status;
}

bool
HttpHeader :: valueForKey
  ( char const * const keystr
  , int const keylen
  , char * const valstr
  , int * const vallen
  , int const index
  ) const
{
  if (! isValid())
  {
    return false;
  }

  bool status = false;

  TSMLoc const locfield = TSMimeHdrFieldFind
      (m_buffer, m_lochdr, keystr, keylen);

  if (nullptr != locfield)
  {
    int getlen = 0;
    char const * const getstr = TSMimeHdrFieldValueStringGet
        (m_buffer, m_lochdr, locfield, 0, &getlen);

    int const valcap = *vallen;
    if (nullptr != getstr && 0 < getlen && getlen < (valcap - 1))
    {
      char * const endp = stpncpy(valstr, getstr, getlen);
      
      *vallen = endp - valstr;
      status = (*vallen < valcap);

      if (status)
      {
        *endp = '\0';
      }
    }
  }

  if (nullptr != locfield)
  {
    TSHandleMLocRelease(m_buffer, m_lochdr, locfield);
  }

  return status;
}

bool
HttpHeader :: setKeyVal
  ( char const * const keystr
  , int const keylen
  , char const * const valstr
  , int const vallen
  , int const index
  )
{
  if (! isValid())
  {
    return false;
  }

  bool status(false);

  TSMLoc locfield
    (TSMimeHdrFieldFind(m_buffer, m_lochdr, keystr, keylen));

  if (nullptr != locfield)
  {
    status = TS_SUCCESS == TSMimeHdrFieldValueStringSet
      (m_buffer, m_lochdr, locfield, index, valstr, vallen);
  }
  else
  {
    int rcode = TSMimeHdrFieldCreateNamed
      (m_buffer, m_lochdr, keystr, keylen, &locfield);

    if (TS_SUCCESS == rcode)
    {
      rcode = TSMimeHdrFieldValueStringSet
        ( m_buffer, m_lochdr, locfield, index, valstr, vallen );
      if (TS_SUCCESS == rcode)
      {
        rcode = TSMimeHdrFieldAppend(m_buffer, m_lochdr, locfield);
        status = (TS_SUCCESS == rcode);
      }
    }
  }

  if (nullptr != locfield)
  {
    TSHandleMLocRelease(m_buffer, m_lochdr, locfield);
  }

  return status;
}

std::string
HttpHeader :: toString
  () const
{
  std::string res;

  if (! isValid())
  {
    return res;
  }

  res.append(method());
  res.append(" ");
/*
  int uslen = 0;
  char const * const urlscheme(urlScheme(&uslen));
  res.append(urlscheme, uslen);
*/
  res.append("\r\n");

  int const numhdrs = TSMimeHdrFieldsCount(m_buffer, m_lochdr);

  for (int indexhdr = 0 ; indexhdr < numhdrs ; ++indexhdr)
  {
    TSMLoc const locfield = TSMimeHdrFieldGet
      (m_buffer, m_lochdr, indexhdr);

    int keylen = 0;
    char const * const keystr = TSMimeHdrFieldNameGet
      (m_buffer, m_lochdr, locfield, &keylen);

    res.append(keystr, keylen);
    res.append(": ");
    int vallen = 0;
    char const * const valstr = TSMimeHdrFieldValueStringGet
      (m_buffer, m_lochdr, locfield, -1, &vallen);

    res.append(valstr, vallen);
    res.append("\r\n");

    TSHandleMLocRelease(m_buffer, m_lochdr, locfield);
  }

  res.append("\r\n");

  return res;
}

/*
int64_t
HttpHeader :: contentBytes
  () const
{
  int64_t bytes(0);
  if (! isValid())
  {
    return bytes;
  }

  TSMLoc const locfield
    ( TSMimeHdrFieldFind
      ( m_buffer
      , m_lochdr
      , TS_MIME_FIELD_CONTENT_LENGTH
      , TS_MIME_LEN_CONTENT_LENGTH ) );

  if (nullptr != locfield)
  {
    bytes = TSMimeHdrFieldValueInt64Get
      (m_buffer, m_lochdr, locfield, -1);
    TSHandleMLocRelease(m_buffer, m_lochdr, locfield);
  }

  return bytes;
}
*/

/////// HdrMgr

TSParseResult
HdrMgr :: populateFrom
  ( TSHttpParser const http_parser
  , TSIOBufferReader const reader
  , HeaderParseFunc const parsefunc
  )
{
  TSParseResult parse_res = TS_PARSE_CONT;

  if (nullptr == m_buffer && nullptr == m_lochdr)
  {
    m_buffer = TSMBufferCreate();
    m_lochdr = TSHttpHdrCreate(m_buffer);
  }

  int64_t read_avail = TSIOBufferReaderAvail(reader);
  if (0 < read_avail)
  {
    TSIOBufferBlock block = TSIOBufferReaderStart(reader);
    int64_t consumed = 0;

    parse_res = TS_PARSE_CONT;

    while (nullptr != block && 0 < read_avail)
    {
      int64_t blockbytes = 0;
      char const * const bstart = TSIOBufferBlockReadStart
        (block, reader, &blockbytes);

      char const * ptr = bstart;
      char const * endptr = ptr + blockbytes;

      parse_res = parsefunc
          ( http_parser
          , m_buffer
          , m_lochdr
          , &ptr
          , endptr );

      int64_t const bytes_parsed(ptr - bstart);

      consumed += bytes_parsed;
      read_avail -= bytes_parsed;

      if (TS_PARSE_CONT == parse_res)
      {
        block = TSIOBufferBlockNext(block);
      }
      else
      {
        break;
      }
    }
    TSIOBufferReaderConsume(reader, consumed);
  }

  return parse_res;
}

bool
HdrMgr :: cloneFrom
  ( TSMBuffer buffersrc
  , TSMLoc locsrc
  )
{
  bool status = false;
  
  if (nullptr == m_buffer && nullptr == m_lochdr)
  {
    m_buffer = TSMBufferCreate();

    TSReturnCode const rcode = TSHttpHdrClone
      (m_buffer, buffersrc, locsrc, &m_lochdr);

    status = (TS_SUCCESS == rcode);
  }

  return status;
}
