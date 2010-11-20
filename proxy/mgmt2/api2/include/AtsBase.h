/** @file
    Apach Traffic Server commons.
    Definitions that are standardized across ATS.
 */

# include <stddef.h>
# include <unistd.h>

/// Apache Traffic Server commons.
namespace ats {
  namespace fixed_integers {
#if !defined(TS_VERSION_STRING)
    /// @name Fixed size integers.
    //@{
    typedef char int8;
    typedef unsigned char uint8;
    typedef short int16;
    typedef unsigned short uint16;
    typedef int int32;
    typedef unsigned int uint32;
    typedef long long int64;
    typedef unsigned long long uint64;
    //@}
#endif
  }
  using namespace fixed_integers;

  /// Standardized null file descriptor.
  int const NO_FD = -1;
}
