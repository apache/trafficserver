#include <ts/MemView.h>
#include <sstream>
#include <ctype.h>

namespace ApacheTrafficServer
{
int
memcmp(MemView const &lhs, MemView const &rhs)
{
  int zret;
  size_t n;

  // Seems a bit ugly but size comparisons must be done anyway to get the memcmp args.
  if (lhs.size() < rhs.size())
    zret = 1, n = lhs.size();
  else {
    n    = rhs.size();
    zret = rhs.size() < lhs.size() ? -1 : 0;
  }

  int r = ::memcmp(lhs.ptr(), rhs.ptr(), n);
  if (0 != r) // If we got a not-equal, override the size based result.
    zret = r;

  return zret;
}

int
strcasecmp(StringView lhs, StringView rhs)
{
  while (lhs && rhs) {
    char l = tolower(*lhs);
    char r = tolower(*rhs);
    if (l < r)
      return -1;
    else if (r < l)
      return 1;
    ++lhs, ++rhs;
  }
  return lhs ? 1 : rhs ? -1 : 0;
}

// Do the template instantions.
template void detail::stream_padding(std::ostream &, std::size_t);
template void detail::aligned_stream_write(std::ostream &, const StringView &);
}

namespace std
{
ostream &
operator<<(ostream &os, const ApacheTrafficServer::MemView &b)
{
  if (os.good()) {
    ostringstream out;
    out << b.size() << '@' << hex << b.ptr();
    os << out.str();
  }
  return os;
}

ostream &
operator<<(ostream &os, const ApacheTrafficServer::StringView &b)
{
  if (os.good()) {
    const size_t size = b.size();
    const size_t w    = static_cast<size_t>(os.width());
    if (w <= size)
      os.write(b.begin(), size);
    else
      ApacheTrafficServer::detail::aligned_stream_write<ostream>(os, b);
    os.width(0);
  }
  return os;
}
}
