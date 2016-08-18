#include <string>
#include "Blob.h"

namespace Magick
{
class Blob;
class Image
{
public:
  Image() {}
  Image(const Blob &blob_) {}
  Image(const Image &image_) {}
  MOCK_METHOD1(assign, Image &(const Image &));
  Image &
  operator=(const Image &image_)
  {
    return assign(image_);
  }

  MOCK_METHOD1(read, void(const Blob &));
  MOCK_METHOD1(magick, void(const std::string &));
  MOCK_METHOD1(write, void(Blob *));
};
}
