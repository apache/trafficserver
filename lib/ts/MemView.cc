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

intmax_t
svtoi(StringView src, StringView *out, int base)
{
  static const int8_t convert[256] = {
    //   0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 00
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 10
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 20
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  -1, -1, -1, -1, -1, -1, // 30
    -1, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, // 40
    25, 26, 27, 28, 20, 30, 31, 32, 33, 34, 35, -1, -1, -1, -1, -1, // 50
    -1, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, // 60
    25, 26, 27, 28, 20, 30, 31, 32, 33, 34, 35, -1, -1, -1, -1, -1, // 70
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 80
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 90
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // A0
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // B0
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // C0
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // D0
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // E0
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // F0
  };

  intmax_t zret = 0;

  if (*out)
    out->clear();
  if (!(1 < base && base <= 36))
    return 0;
  if (src.ltrim(&isspace)) {
    const char *start = src.ptr();
    int8_t v;
    bool neg = false;
    if ('-' == *src) {
      ++src;
      neg = true;
    }
    while (src.size() && (-1 != (v = convert[static_cast<unsigned char>(*src)]))) {
      zret = zret * base + v;
      ++src;
    }
    if (out && (src.ptr() > (neg ? start + 1 : start))) {
      out->setView(start, src.ptr());
    }

    if (neg)
      zret = -zret;
  }
  return zret;
}

// Do the template instantions.
template void detail::stream_fill(std::ostream &, std::size_t);
template std::ostream &StringView::stream_write(std::ostream &, const StringView &) const;
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
    b.stream_write<ostream>(os, b);
    os.width(0);
  }
  return os;
}
}
