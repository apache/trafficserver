// This is for PCRE, where the offsets vector is considered uninitialized, but it's an
//  output vector only (input doesn't matter).
extern "C" {
#define PCRE_SPTR const char *

struct real_pcre;   /* declaration; the definition is private  */
typedef struct real_pcre pcre;

typedef struct pcre_extra {
} pcre_extra;

int 
pcre_exec(const pcre *argument_re, const pcre_extra *extra_data,
          PCRE_SPTR subject, int length, int start_offset, int options, int *offsets,
          int offsetcount)
{
  __coverity_panic__();
}
} /* extern "C" */
