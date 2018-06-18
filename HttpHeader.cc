#include "HttpHeader.h"

#include "slice.h"

#include <cassert>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <limits>


TSHttpType
HttpHeader :: type
  () const
{
  if (isValid())
  {
    return TSHttpHdrTypeGet(m_buffer, m_lochdr);
  }
  else
  {
    return TS_HTTP_TYPE_UNKNOWN;
  }
}

TSHttpStatus
HttpHeader :: status
  () const
{
  TSHttpStatus res = TS_HTTP_STATUS_NONE;
  if (isValid())
  {
    res = TSHttpHdrStatusGet(m_buffer, m_lochdr);
  }
  return res;
}

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
    return "<null>";
  }

  TSHttpType const htype(type());

  switch (htype)
  {
    case TS_HTTP_TYPE_REQUEST:
    {
      res.append(method());

      TSMLoc locurl = nullptr;
      TSReturnCode const rcode = TSHttpHdrUrlGet
        (m_buffer, m_lochdr, &locurl);
      if (TS_SUCCESS == rcode && nullptr != locurl)
      {
        int urllen = 0;
        char * const urlstr = TSUrlStringGet
          (m_buffer, locurl, &urllen);
        res.append(" ");
        res.append(urlstr, urllen);
        TSfree(urlstr);

        TSHandleMLocRelease(m_buffer, m_lochdr, locurl);
      }
      else
      {
        res.append(" UnknownURL");
      }

      res.append(" HTTP/unparsed");
    }
    break;

    case TS_HTTP_TYPE_RESPONSE:
    {
      char bufstr[1024];
/*
      int const version = TSHttpHdrVersionGet(m_buffer, m_lochdr);
      snprintf(bufstr, 1023, "%d ", version);
      res.append(bufstr);
*/
      res.append("HTTP/unparsed");

      int const status = TSHttpHdrStatusGet(m_buffer, m_lochdr);
      snprintf(bufstr, 1023, " %d ", status);
      res.append(bufstr);

      int reasonlen = 0;
      char const * const hreason = reason(&reasonlen);

      res.append(hreason, reasonlen);
    }
    break;

    default:
    case TS_HTTP_TYPE_UNKNOWN:
      res.append("UNKNOWN");
    break;
  }

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
