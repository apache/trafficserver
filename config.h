
#pragma once

#include "ts/ts.h"

// Data Structures and Classes
class SliceConfig
{
public:

  static const int64_t min_blocksize           = 1024 * 512;        // 512KB
  static const int64_t max_blocksize           = 1024 * 1024 * 32;  // 32MB
  static const int64_t default_watermark_bytes = 1024 * 1024;       // 1MB
  static const int64_t default_blocksize       = min_blocksize * 2; // 2MB

public:

  int64_t m_blocksize{default_blocksize};
  int64_t m_input_wm_bytes{512 * 1024};  // watermark
  int64_t m_output_wm_bytes{512 * 1024}; // watermark

  SliceConfig();
  ~SliceConfig();

  bool parseArguments(int const argc, char const * const argv[], char * const errbuf, int const errbuf_size);

  int64_t blockSize() const;

  int64_t inputWatermarkBytes() const;

  int64_t outputWatermarkBytes() const;
};
