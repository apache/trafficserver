1a\
namespace ts { namespace config {\
  enum TokenType {
1,/^  *enum/d
/^ *{/d
/^  *};/a\
}} // namespace ts::config
/^#endif/,$d
