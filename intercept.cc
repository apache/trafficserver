#include "intercept.h"

#include "Data.h"

#include <iostream>

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
handle_client_req
  ( TSCont contp
  , TSEvent event
  , Data * const data
  )
{
  // at the moment toss, we'll make one up
  if (TS_EVENT_VCONN_READ_READY == event)
  {
    // parse incoming request header here
    int64_t const bytesavail
        (TSIOBufferReaderAvail(data->m_dnstream.m_read.m_reader));
std::cerr << "Incoming header: " << bytesavail << std::endl;
    TSIOBufferReaderConsume(data->m_dnstream.m_read.m_reader, bytesavail);

    // create header
    std::string const get_request
      (rangeRequestStringFor("bytes=0-1048575"));

std::cerr << get_request << std::endl;

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
    && data->m_upstream.m_read.isValid() )  // temporary! 
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

std::cerr << "intercept_hook event: " << event << std::endl;

  if (TS_EVENT_NET_ACCEPT == event)
  {
    // set up reader from client
    TSVConn const downvc = (TSVConn)edata;
    data->m_dnstream.setupConnection(downvc);
    data->m_dnstream.setupVioRead(contp);
    data->m_dnstream.setupVioWrite(contp);
  }
  else // more data from client
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
    // already sent, not interested
    TSVConnShutdown(data->m_upstream.m_vc, 0, 1);
  }
  else // server has data for us
  if ( data->m_upstream.m_read.isValid()
    && edata == data->m_upstream.m_read.m_vio )
  {
std::cerr << "server has data for us" << std::endl;
    handle_server_resp(contp, event, data);
  }
  else // client wants more data from us
  if ( data->m_dnstream.m_write.isValid()
    && edata == data->m_dnstream.m_write.m_vio )
  {
std::cerr << "client wants data from us" << std::endl;
    handle_client_resp(contp, event, data);
  }
  else
  {
std::cerr << "unhandled event: " << event << std::endl;
  }

/*
*/

  return 0;
}
