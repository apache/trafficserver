#ifndef PNGDEC_H_
#define PNGDEC_H_

#include <stdio.h>
#include <string>
#include <sstream>
#include <png.h>
#include <webp/types.h>
#include "pngdec.h"
#include "metadata.h"

struct WebPPicture;

class PngDec {
public:
	PngDec() : init_(false), input_img_(NULL), info_(NULL), end_info_(NULL)
	{}
	~PngDec() { }
	bool Init(std::stringstream* img);
	int ReadImage(struct WebPPicture* const pic, struct Metadata* const metadata);
	void Finalize();

#ifndef UNIT_TESTING
private:
#endif

	struct PNGMetadataMap {
	  const char* name;
	  int (*process)(const char* profile, size_t profile_len,
	                 MetadataPayload* const payload);
	  size_t storage_offset;
	};

	void Read(png_structp pngPtr, png_bytep data, png_size_t length);
	int ExtractMetadataFromPNG(Metadata* const metadata);

	static uint8_t* HexStringToBytes(const char* hexstring, size_t expected_length);
	static int ProcessRawProfile(const char* profile, size_t profile_len, MetadataPayload* const payload);
	static void ReadFunction(png_structp pngPtr, png_bytep data, png_size_t length);
	static void ErrorFunction(png_structp png, png_const_charp error);
	bool ReadData(png_bytep data, png_size_t len);


	bool init_;
	std::stringstream*   input_img_;
  volatile png_structp png_;
  volatile png_infop   info_;
  volatile png_infop   end_info_;
	static PNGMetadataMap png_metadata_map_[];
};
#endif  // PNGDEC_H_
