#pragma once

#include "ts/ts.h"

struct HttpHeader
{
  HttpHeader(HttpHeader const &) = delete;
  HttpHeader & operator=(HttpHeader const &) = delete;

  TSMBuffer m_buffer;
  TSMLoc m_lochdr;

  HttpHeader
    ()
    : m_buffer(TSMBufferCreate())
    , m_lochdr(TSHttpHdrCreate(m_buffer))
  {
  }

  ~HttpHeader
    ()
  {
    TSHttpHdrDestroy(m_buffer, m_lochdr);
    TSHandleMLocRelease(m_buffer, TS_NULL_MLOC, m_lochdr);
    TSMBufferDestroy(m_buffer);
  }
};
