#ifndef _ESI_GZIP_H
#define _ESI_GZIP_H

#include "ComponentBase.h"
#include <zlib.h>
#include <string>

class EsiGzip : private EsiLib::ComponentBase
{

public:

  EsiGzip(const char *debug_tag,
               EsiLib::ComponentBase::Debug debug_func, EsiLib::ComponentBase::Error error_func);

  virtual ~EsiGzip();

  bool stream_encode(const char *data, int data_len, std::string &cdata);

  inline bool stream_encode(std::string data, std::string &cdata) {
    return stream_encode(data.data(), data.size(), cdata);
  }

  bool stream_finish(std::string &cdata, int&downstream_length);

private:

  //int runDeflateLoop(z_stream &zstrm, int flush, std::string &cdata);
  int _downstream_length;
  int _total_data_length;
  z_stream _zstrm;
  uLong _crc;
};


#endif // _ESI_GZIP_H

