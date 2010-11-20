# if ! defined(TS_CONFIG_TYPES_HEADER)
# define TS_CONFIG_TYPES_HEADER

# if defined(_MSC_VER)
#   include <stddef.h>
# else
#   include <unistd.h>
# endif

# if defined(__cplusplus)
namespace ts { namespace config {
# endif

/** A location in the source stream.
    @internal At some point we may need to add stream information,
    e.g. file name, once includes are supported. Or should that
    be the caller's responsibility?
 */
struct Location {
    int _col; ///< Column.
    int _line; ///< Line.
};

/** A token from the source stream.
    @internal We should use ts::Buffer here, but because this
    has to work in C as well, it's less painful to do it by hand.
 */
struct Token {
  char* _s; ///< Text of token.
  size_t _n; ///< Text length.
  int _type; ///< Type of token.
  struct Location _loc; ///< Location of token.
};

# if defined(__cplusplus)
}} // namespace ts::config
# define YYSTYPE ts::config::Token
# else
# define YYSTYPE struct Token
#endif


# endif // TS_CONFIG_TYPES_HEADER
