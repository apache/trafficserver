#include "intercept.h"

#include "Data.h"
#include "range.h"

#include <cinttypes>
#include <iostream>

std::string
simClientRequestHeader
  ()
{
  std::string get_request;
  get_request.append(TS_HTTP_METHOD_GET);
  get_request.append(" /voidlinux.iso HTTP/1.1\r\n");
  get_request.append(TS_MIME_FIELD_HOST);
  get_request.append(": localhost:6010\r\n");
  get_request.append(TS_MIME_FIELD_USER_AGENT);
  get_request.append(": ATS/slicer\r\n");
  get_request.append(TS_MIME_FIELD_ACCEPT);
  get_request.append(": */*\r\n");
  get_request.append("\r\n");
  return get_request;
}

std::string
rangeRequestStringFor
  ( std::string const & bytesstr
  )
{
  std::string get_request;
  get_request.append(TS_HTTP_METHOD_GET);
  get_request.append(" /voidlinux.iso HTTP/1.1\r\n");
  get_request.append(TS_MIME_FIELD_HOST);
  get_request.append(": localhost:6010\r\n");
  get_request.append(TS_MIME_FIELD_USER_AGENT);
  get_request.append(": ATS/whatever\r\n");
  get_request.append(TS_MIME_FIELD_ACCEPT);
  get_request.append(": */*\r\n");
  get_request.append(TS_MIME_FIELD_RANGE);
  get_request.append(": ");
  get_request.append(bytesstr);
  get_request.append("\r\n");
  get_request.append("X-Skip-Me");
  get_request.append(": absolutely\r\n");
  get_request.append("\r\n");
  return get_request;
}

static
int
request_block
  ( TSCont contp
  , Data * const data
  )
{
  std::pair<int64_t, int64_t> const blockrange
    (rangeForBlock(data->m_blocksize, data->m_blocknum));

  char content_range[1024];
  int const content_range_len(snprintf
    ( content_range, 1024
    , "bytes=%" PRId64 "-%" PRId64
    , blockrange.first, blockrange.second - 1 ) );
TSAssert(0 < content_range_len);

  // create header
  std::string const get_request
    (rangeRequestStringFor(content_range));

std::cerr << __func__ << '\n' << get_request << std::endl;

  // virtual connection
  TSVConn const upvc = TSHttpConnect((sockaddr*)&data->m_client_ip);

  // set up connection with the HttpConnect server
  data->m_upstream.setupConnection(upvc);
  data->m_upstream.setupVioWrite(contp);

  TSIOBufferWrite
    ( data->m_upstream.m_write.m_iobuf
    , get_request.data()
    , get_request.size() );

  TSVIOReenable(data->m_upstream.m_write.m_vio);

  // get ready for data back from the server
  data->m_upstream.setupVioRead(contp);

  // reset the header parsed flag
  data->m_server_res_header_parsed = false;

  return 0;
}

static
int
handle_client_req
  ( TSCont contp
  , TSEvent event
  , Data * const data
  )
{
  if (TS_EVENT_VCONN_READ_READY == event)
  {
    // parse incoming request header here (TOSS FOR TEST)
    int64_t const bytesavail
        (TSIOBufferReaderAvail(data->m_dnstream.m_read.m_reader));
std::cerr << __func__ << ": incoming header: " << bytesavail << std::endl;
    TSIOBufferReaderConsume(data->m_dnstream.m_read.m_reader, bytesavail);

    // simulate request header
    std::string const req_header(simClientRequestHeader());

    std::pair<int64_t, int64_t> const rangebegend(0, 1024 * 1024 * 2);

    data->m_range_begend = rangebegend;
    data->m_blocknum = firstBlockInRange(data->m_blocksize, rangebegend);

    request_block(contp, data);
  }

  return 0;
}

static
int
handle_server_resp
  ( TSCont contp
  , TSEvent event
  , Data * const data
  )
{
  if (TS_EVENT_VCONN_READ_READY == event)
  {
    // do we expect a header???
/*
    if (! data->m_server_res_header_parsed)
    {
      TSHttpParser const http_parser = data->httpParse();
TSAssert(nullptr != parser);

      TSIOBufferBlock block = TSIOBufferReaderStart
        (data->m_upstream.m_read.m_reader);
      while (nullptr != block && ! data->m_server_res_header_parsed)
      {
        int64_t datalen = 0;
        char const * const data = TSIOBufferBlockReadStart
            (block, data->m_upstream.m_read.m_reader, datalen);
TSAssert(nullptr != data);
        char const * endptr = data + datalen;

        TSParseResult const parseres = TSHttpHdrParseReq
            ( http_parser
            , data->m_upstream.m_read.m_iobuf
            , ?req // BNOBNO

        if (TS_PARSE_DONE == parseres)
        {
        }
      }

      data->m_server_res_header_parsed = true;
    }
*/



    // otherwise


    int64_t read_avail
      (TSIOBufferReaderAvail(data->m_upstream.m_read.m_reader));

    if (0 < read_avail)
    {
      int64_t const copied
        ( TSIOBufferCopy
          ( data->m_dnstream.m_write.m_iobuf
          , data->m_upstream.m_read.m_reader
          , read_avail
          , 0 ) );
std::cerr << __func__ << ": copied: " << copied << std::endl;

      TSIOBufferReaderConsume(data->m_upstream.m_read.m_reader, copied);

      TSVIOReenable(data->m_dnstream.m_write.m_vio);

/*
      int64_t consumed(0);
      TSIOBufferBlock block = TSIOBufferReaderStart
          (data->upstream->read->reader);

      while (nullptr != block && 0 < read_avail)
      {
        int64_t read_block(0);
        char const * const block_start = TSIOBufferBlockReadStart
            (block, data->upstream->read->reader, &read_block);
        if (0 < read_block)
        {
          int64_t const tocopy(std::min(read_block, read_avail));
          std::cerr << std::string(block_start, block_start + tocopy);
          read_avail -= tocopy;
          consumed += tocopy;
          block = TSIOBufferBlockNext(block);
        }
      }

      TSIOBufferReaderConsume(data->upstream->read->reader, consumed);
*/
    }
  }
  else
  {
std::cerr << __func__ << ": unhandled event: " << event << std::endl;
  }

  return 0;
}

static
int
handle_client_resp
  ( TSCont contp
  , TSEvent event
  , Data * const data
  )
{
  if (TS_EVENT_VCONN_WRITE_READY == event
    && data->m_upstream.m_read.isValid() )  // temporary
  {
    int64_t read_avail
      (TSIOBufferReaderAvail(data->m_upstream.m_read.m_reader));

    if (0 < read_avail)
    {
      int64_t const copied
        ( TSIOBufferCopy
          ( data->m_dnstream.m_write.m_iobuf
          , data->m_upstream.m_read.m_reader
          , read_avail
          , 0 ) );

std::cerr << "copied: " << copied << std::endl;

      TSIOBufferReaderConsume(data->m_upstream.m_read.m_reader, copied);

      TSVIOReenable(data->m_dnstream.m_write.m_vio);

/*
      int64_t consumed(0);
      TSIOBufferBlock block = TSIOBufferReaderStart
          (data->upstream->read->reader);

      while (nullptr != block && 0 < read_avail)
      {
        int64_t read_block(0);
        char const * const block_start = TSIOBufferBlockReadStart
            (block, data->upstream->read->reader, &read_block);
        if (0 < read_block)
        {
          int64_t const tocopy(std::min(read_block, read_avail));
          std::cerr << std::string(block_start, block_start + tocopy);
          read_avail -= tocopy;
          consumed += tocopy;
          block = TSIOBufferBlockNext(block);
        }
      }

      TSIOBufferReaderConsume(data->upstream->read->reader, consumed);
*/
    }
    else // close it all out???
    {
std::cerr << __func__ << ": unhandled event: " << event << std::endl;
    }
  }

  return 0;
}

int
intercept_hook
  ( TSCont contp
  , TSEvent event
  , void * edata
  )
{
  Data * const data = (Data*)TSContDataGet(contp);

std::cerr << __func__ << ": event: " << event << std::endl;

  // After the initial TS_EVENT_NET_ACCEPT
  // any "events" will be handled by the vio read or write channel handler
  if (TS_EVENT_NET_ACCEPT == event)
  {
    // set up reader from client
    TSVConn const downvc = (TSVConn)edata;
    data->m_dnstream.setupConnection(downvc);
    data->m_dnstream.setupVioRead(contp);

    // this should be set up only when we actually send
    // a header back to the client.  We still need
    // to handle times when we have no data yet
    // (in between blocks, etc) when the
    // client still wants more.
    data->m_dnstream.setupVioWrite(contp);
  }
  else // data from client -- only the initial header
  if ( data->m_dnstream.m_read.isValid()
    && edata == data->m_dnstream.m_read.m_vio )
  {
    handle_client_req(contp, event, data);
    TSVConnShutdown(data->m_dnstream.m_vc, 1, 0);
  }
  else // server wants more data from us
  if ( data->m_upstream.m_write.isValid()
    && edata == data->m_upstream.m_write.m_vio )
  {
    // header was already sent, not interested
    TSVConnShutdown(data->m_upstream.m_vc, 0, 1);
  }
  else // server has data for us
  if ( data->m_upstream.m_read.isValid()
    && edata == data->m_upstream.m_read.m_vio )
  {
    handle_server_resp(contp, event, data);
  }
  else // client wants more data from us
  if ( data->m_dnstream.m_write.isValid()
    && edata == data->m_dnstream.m_write.m_vio )
  {
    handle_client_resp(contp, event, data);
  }
  else
  {
std::cerr << __func__ << ": unhandled event: " << event << std::endl;
  }

  return 0;
}
