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

  /** A chunk of memory.
      A convenience class because we pass this kind of pair frequently.
   */
  struct Buffer {
    typedef Buffer self; ///< Self reference type.

    char* m_ptr; ///< Pointer to base of memory chunk.
    size_t m_size; ///< Size of memory chunk.

    /// Default constructor.
    /// Elements are in unintialized state.
    Buffer();

    /// Construct from pointer and size.
    Buffer(
      char* ptr, ///< Pointer to buffer.
      size_t n ///< Size of buffer.
    );

    /// Set the chunk.
    /// Any previous values are discarded.
    /// @return @c this object.
    self& set(
      char* ptr, ///< Buffer address.
      size_t n ///< Buffer size.
    );
  };

  /** Base class for ATS exception.
      Clients should subclass as appropriate. This is intended to carry
      pre-allocated text along so that it can be thrown without any
      adddition memory allocation.
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

  inline Buffer::Buffer() { }
  inline Buffer::Buffer(char* ptr, size_t n) : m_ptr(ptr), m_size(n) { }
  inline Buffer& Buffer::set(char* ptr, size_t n) {
    m_ptr = ptr;
    m_size = n;
    return *this;
  }

  inline Exception::Exception() : m_text(DEFAULT_TEXT) { }
  inline Exception::Exception(char const* text) : m_text(text) { }
}
