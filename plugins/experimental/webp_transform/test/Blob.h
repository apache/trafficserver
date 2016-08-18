#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <string>

namespace Magick
{
class Blob
{
public:
  Blob() {}
  Blob(const void *data_, const size_t length_) {}
  Blob(const Blob &blob_) {}
  MOCK_METHOD1(assign, Blob &(const Blob &));
  Blob &
  operator=(const Blob &blob_)
  {
    return assign(blob_);
  }

  MOCK_CONST_METHOD1(compare, bool(const Blob &));
  bool
  operator==(const Blob &rhs) const
  {
    return compare(rhs);
  }

  MOCK_CONST_METHOD0(data, const void *());
  MOCK_CONST_METHOD0(length, size_t());
  MOCK_METHOD2(update, void(const void *data, const size_t length));
};
}
