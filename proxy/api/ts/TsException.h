# if ! defined(TS_EXCEPTION_HEADER)
# define TS_EXCEPTION_HEADER

/** @file
    Apach Traffic Server Exceptions.
 */

# include <stddef.h>
# include <unistd.h>

namespace ts {
  /** Base class for ATS exception.
      Clients should subclass as appropriate. This is intended to carry
      pre-allocated text along so that it can be thrown without any
      addditional memory allocation.
  */
  class Exception {
  public:
    /// Default constructor.
    Exception();
    /// Construct with alternate @a text.
    Exception(
      const char* text ///< Alternate text for exception.
    );

    static char const* const DEFAULT_TEXT;
  protected:
    char const* m_text;
  };

  // ----------------------------------------------------------
  // Inline implementations.

  inline Exception::Exception() : m_text(DEFAULT_TEXT) { }
  inline Exception::Exception(char const* text) : m_text(text) { }
}

# endif // TS_EXCEPTION_HEADER
