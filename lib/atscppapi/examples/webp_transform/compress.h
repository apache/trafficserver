
#ifndef WEBPCOMPRESS_H_
#define WEBPTRANSFORM_H_

#include <stdio.h>
#include <sstream>
#include <string>
#include <vector>

#include <webp/encode.h>
#include "jpegdec.h"
#include "pngdec.h"
#include "metadata.h"


class WebpTransform {
public:
	WebpTransform() :init_(false), png_dec_(), jpeg_dec_()
	{ }

	~WebpTransform() { }

	bool Init();
	bool Transform(std::stringstream& stream);
	void Finalize();
	std::stringstream& getTransformedImage() { return stream_; }
	void WriteImage(const char* data, size_t data_size);

#ifndef UNIT_TESTING
private:
#endif
	typedef enum {
	  PNG_ = 0,
	  JPEG_,
	  TIFF_,  // 'TIFF' clashes with libtiff
	  WEBP_,
	  UNSUPPORTED
	} InputFileFormat;

	enum {
	  METADATA_EXIF = (1 << 0),
	  METADATA_ICC  = (1 << 1),
	  METADATA_XMP  = (1 << 2),
	  METADATA_ALL  = METADATA_EXIF | METADATA_ICC | METADATA_XMP
	};

	InputFileFormat GetImageType(std::stringstream& input_msg);
	int ReadImage(std::stringstream& input_img);
	void AllocExtraInfo();

	void WebPMemoryWriterClear();

	static const std::string errors_[];
	bool                init_;
	WebPMemoryWriter    writer_;
	std::stringstream   stream_;
  WebPPicture         picture_;
  WebPConfig          config_;
	Metadata            metadata_;
	std::string         debug_tag_;
	PngDec              png_dec_;
	JpegDec             jpeg_dec_;
};

#endif  // COMPRESS_H_
