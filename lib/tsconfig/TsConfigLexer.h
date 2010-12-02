# if ! defined(TS_CONFIG_LEXER_HEADER)
# define TS_CONFIG_LEXER_HEADER

struct TsConfigHandlers; // forward declare.

# if defined(__cplusplus)
    extern "C" {
# endif

// extern int tsconfigparse(yyscan_t lexer, struct TsConfigHandlers* handlers);

/// Get the current line in the buffer during parsing.
/// @return 1 based line number.
extern int tsconfiglex_current_line(void);
/// Get the current column in the buffer during parsing.
/// @return 0 base column number.
extern int tsconfiglex_current_col(void);

/** Parse @a buffer.

    The @a buffer is parsed and the events dispatched via @a handlers.

    @note The contents of @a buffer are in general modified to handle
    null termination and processing escape codes in strings. Tokens
    passed to the handlers are simply offsets in to @a buffer and
    therefore have the same lifetime as @a buffer.

    @return Not sure.
 */
extern int tsconfig_parse_buffer(
  struct TsConfigHandlers* handlers, ///< Syntax handlers.
  char* buffer, ///< Input buffer.
  size_t buffer_len ///< Length of input buffer.
);

# if defined(__cplusplus)
}
# endif

# endif // TS_CONFIG_LEXER_HEADER
