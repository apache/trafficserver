#pragma once
#include <vector>
#include "pcre.h"

#include "ts/ink_memory.h"

struct Scrub {
  ~Scrub()
  {
    if (pattern) {
      ats_free(const_cast<char *>(pattern));
    }
    if (replacement) {
      ats_free(const_cast<char *>(replacement));
    }
    if (compiled_re) {
      pcre_free(const_cast<pcre *>(compiled_re));
    }
  }
  static const int OVECCOUNT = 30;
  const char *pattern;
  const char *replacement;
  const pcre *compiled_re;
  int ovector[OVECCOUNT];
};

/*
 * Class that helps scrub specific strings from buffers
 */
class Scrubber
{
public:
  /*
   * Parses config & constructs Scrubber
   */
  Scrubber(const char *config);
  Scrubber(Scrubber &other) = delete;
  ~Scrubber();

  /*
   * Add another expression to scrub for
   *
   * @returns whether or not the addition was successful
   */
  bool scrub_add(const char *pattern, const char *replacement);

  /*
   * Heap allocates an identical buffer that is scrubbed with multiple Scrubs.
   * Caller should ats_free.
   */
  char *scrub_buffer(const char *buffer) const;

  char *get_config() { return config; };

private:
  /*
   * Heap allocates an identical buffer that is scrubbed.
   * Caller should ats_free.
   */
  char *scrub_buffer(const char *buffer, Scrub *scrub) const;

  char *config = nullptr;
  std::vector<Scrub *> scrubs;
};
