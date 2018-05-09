#include "config.h"
#include "slicer.h"
#include "util.h"

#include <cctype>
#include <fstream>
#include <string>

static char const *const SLICER_CONFIG_BLOCKSIZE       = "blocksize";
static char const *const SLICER_CONFIG_INPUT_WM_BYTES  = "input_watermark_bytes";
static char const *const SLICER_CONFIG_OUTPUT_WM_BYTES = "output_watermark_bytes";

SlicerConfig :: SlicerConfig
	()
    : m_blocksize(default_blocksize)
	 , m_input_wm_bytes(default_watermark_bytes)
	 , m_output_wm_bytes(default_watermark_bytes)
{
  ALLOC_DEBUG_LOG("SlicerConfig - Create");
}

SlicerConfig :: ~SlicerConfig()
{
  DEBUG_LOG("SlicerConfig - Destroy");
}

static inline bool
isPowerOfTwo(int64_t value)
{
  return (0 != value) && (0 == (value & (value - 1)));
}

bool
SlicerConfig :: parseArguments(int const argc, char const *const argv[], char *const errbuf, int const errbuf_size)
{
  DEBUG_LOG("Number of arguments: %d", argc);
  for (int ii = 0; ii < argc; ++ii) {
    DEBUG_LOG("args[%d] = %s", ii, argv[ii]);
  }

  std::string pathname;
  if (2 < argc) {
    std::string const fname(argv[2]);

    if ('/' != fname[0]) {
      pathname = std::string(TSConfigDirGet()) + '/' + fname;
    } else {
      pathname = std::move(fname);
    }
  }

  if (!pathname.empty()) {
    std::ifstream ifs(pathname.c_str());
    if (!ifs) {
      DEBUG_LOG("Config file not found: %s, using default", pathname.c_str());
    } else {
      int lineno(0);
      std::string line;
      while (std::getline(ifs, line)) {
        std::pair<std::string, std::string> const keyval(keyValFrom(std::move(line)));

        std::string const &key = keyval.first;
        std::string const &val = keyval.second;

        DEBUG_LOG("Line: %d Key: '%s' Val: '%s'", lineno, key.c_str(), val.c_str());

        if (!val.empty() && !key.empty()) {
          if (key == SLICER_CONFIG_BLOCKSIZE) {
            m_blocksize = atoll(val.c_str());
          } else if (key == SLICER_CONFIG_INPUT_WM_BYTES) {
            m_input_wm_bytes = atoll(val.c_str());
          } else if (key == SLICER_CONFIG_OUTPUT_WM_BYTES) {
            m_output_wm_bytes = atoll(val.c_str());
          }
        }

        ++lineno;
      }
    }
  } else {
    DEBUG_LOG("Using default Slicer configuration");
  }

  if (m_blocksize < min_blocksize) {
    ERROR_LOG("%s: %ld less than min %ld", SLICER_CONFIG_BLOCKSIZE, m_blocksize, min_blocksize);
  } else if (max_blocksize < m_blocksize) {
    ERROR_LOG("%s: %ld more than max %ld", SLICER_CONFIG_BLOCKSIZE, m_blocksize, max_blocksize);
  }

  if (m_input_wm_bytes < min_blocksize) {
    ERROR_LOG("%s: %ld less than min %ld", SLICER_CONFIG_INPUT_WM_BYTES, m_input_wm_bytes, min_blocksize);
  } else if (max_blocksize < m_input_wm_bytes) {
    ERROR_LOG("%s: %ld more than max %ld", SLICER_CONFIG_INPUT_WM_BYTES, m_input_wm_bytes, max_blocksize);
  }

  if (m_output_wm_bytes < min_blocksize) {
    ERROR_LOG("%s: %ld less than min %ld", SLICER_CONFIG_OUTPUT_WM_BYTES, m_output_wm_bytes, min_blocksize);
  } else if (max_blocksize < m_output_wm_bytes) {
    ERROR_LOG("%s: %ld more than max %ld", SLICER_CONFIG_OUTPUT_WM_BYTES, m_output_wm_bytes, max_blocksize);
  }

  if (!isPowerOfTwo(m_blocksize)) {
    ERROR_LOG("%s: %ld not a power of 2", SLICER_CONFIG_BLOCKSIZE, m_blocksize);
  }
  if (!isPowerOfTwo(m_input_wm_bytes)) {
    ERROR_LOG("%s: %ld not a power of 2", SLICER_CONFIG_INPUT_WM_BYTES, m_input_wm_bytes);
  }
  if (!isPowerOfTwo(m_output_wm_bytes)) {
    ERROR_LOG("%s: %ld not a power of 2", SLICER_CONFIG_OUTPUT_WM_BYTES, m_output_wm_bytes);
  }

  DEBUG_LOG("%s: %ld", SLICER_CONFIG_BLOCKSIZE, m_blocksize);
  DEBUG_LOG("%s %ld", SLICER_CONFIG_INPUT_WM_BYTES, m_input_wm_bytes);
  DEBUG_LOG("%s %ld", SLICER_CONFIG_OUTPUT_WM_BYTES, m_output_wm_bytes);

  return true;
}

int64_t
SlicerConfig :: blockSize() const
{
  return m_blocksize;
}

int64_t
SlicerConfig :: inputWatermarkBytes() const
{
  return m_input_wm_bytes;
}

int64_t
SlicerConfig :: outputWatermarkBytes() const
{
  return m_output_wm_bytes;
}
