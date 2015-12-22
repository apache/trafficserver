/** @file

    ATSCPPAPI plugin to do webp transform.

    @section license License

    Licensed to the Apache Software Foundation (ASF) under one
    or more contributor license agreements.  See the NOTICE file
    distributed with this work for additional information
    regarding copyright ownership.  The ASF licenses this file
    to you under the Apache License, Version 2.0 (the
    "License"); you may not use this file except in compliance
    with the License.  You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#ifndef WEBPTRANSFORM_H_
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
