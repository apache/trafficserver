# if ! defined(TS_CONFIG_PARSE_EVENTS_HEADER)
# define TS_CONFIG_PARSE_EVENTS_HEADER

// Parsing events.

# include "TsConfigTypes.h"

typedef void (*TsConfigEventFunction)(void* data, YYSTYPE* token);
struct TsConfigEventHandler {
    TsConfigEventFunction _f; ///< Callback function.
    void* _data; ///< Callback context data.
};
typedef int (*TsConfigErrorFunction)(void* data, char const* text);
struct TsConfigErrorHandler {
    TsConfigErrorFunction _f; ///< Callback function.
    void* _data; ///< Callback context data.
};

enum TsConfigEventType {
    TsConfigEventGroupOpen,
    TsConfigEventGroupName,
    TsConfigEventGroupClose,
    TsConfigEventListOpen,
    TsConfigEventListClose,
    TsConfigEventPathOpen,
    TsConfigEventPathTag,
    TsConfigEventPathIndex,
    TsConfigEventPathClose,
    TsConfigEventLiteralValue,
    TsConfigEventInvalidToken
};

# if defined(__cplusplus)
static const size_t TS_CONFIG_N_EVENT_TYPES = TsConfigEventInvalidToken + 1;
# else
# define TS_CONFIG_N_EVENT_TYPES (TsConfigEventInvalidToken + 1)
# endif

struct TsConfigHandlers {
    struct TsConfigErrorHandler error; ///< Syntax error.
    /// Parsing event handlers.
    /// Indexed by @c TsConfigEventType.
    struct TsConfigEventHandler handler[TS_CONFIG_N_EVENT_TYPES];
};

# endif // TS_CONFIG_PARSE_EVENTS_HEADER
