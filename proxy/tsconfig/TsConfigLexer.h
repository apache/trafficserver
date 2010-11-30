# if ! defined(TS_CONFIG_LEXER_HEADER)
# define TS_CONFIG_LEXER_HEADER

// Need this for TsConfigHandlers
# include "TsConfigParseEvents.h"

# if defined(__cplusplus)
    extern "C" {
# endif
#   include "TsConfig.lex.h"
    extern int tsconfigparse(yyscan_t lexer, struct TsConfigHandlers* handlers);
# if defined(__cplusplus)
}
# endif

# endif // TS_CONFIG_LEXER_HEADER
