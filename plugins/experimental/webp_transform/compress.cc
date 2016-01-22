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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include <sstream>
#include <atscppapi/Logger.h>
#include "compress.h"
#include "Common.h"

#define MAGIC_SIZE 12;
using std::string;
using std::vector;


static int
StreamWriter(const uint8_t *data, size_t data_size, const WebPPicture *const pic)
{
  WebpTransform *webp_transform = static_cast<WebpTransform *>(pic->custom_ptr);
  webp_transform->writeImage(reinterpret_cast<const char *>(data), data_size);
  return data_size ? data_size : 1;
}

const string WebpTransform::_errors[] = {
  "OK", "OUT_OF_MEMORY: Out of memory allocating objects", "BITSTREAM_OUT_OF_MEMORY: Out of memory re-allocating byte buffer",
  "NULL_PARAMETER: NULL parameter passed to function", "INVALID_CONFIGURATION: configuration is invalid",
  "BAD_DIMENSION: Bad picture dimension. Maximum width and height "
  "allowed is 16383 pixels.",
  "PARTITION0_OVERFLOW: Partition #0 is too big to fit 512k.\n"
  "To reduce the size of this partition, try using less segments "
  "with the -segments option, and eventually reduce the number of "
  "header bits using -partition_limit. More details are available "
  "in the manual (`man cwebp`)",
  "PARTITION_OVERFLOW: Partition is too big to fit 16M", "BAD_WRITE: Picture writer returned an I/O error",
  "FILE_TOO_BIG: File would be too big to fit in 4G", "USER_ABORT: encoding abort requested by user"};

void
WebpTransform::_webpMemoryWriterClear()
{
  if (_writer.mem != NULL) {
    free(_writer.mem);
    _writer.mem = NULL;
    _writer.size = 0;
    _writer.max_size = 0;
  }
}


WebpTransform::InputFileFormat
WebpTransform::_getImageType(std::stringstream &input_img)
{
  InputFileFormat format = UNSUPPORTED;
  uint32_t magic1, magic2;
  uint8_t buf[12];
  input_img.read((char *)buf, 12);
  input_img.seekg(0, input_img.beg);

  magic1 = ((uint32_t)buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
  magic2 = ((uint32_t)buf[8] << 24) | (buf[9] << 16) | (buf[10] << 8) | buf[11];
  if (magic1 == 0x89504E47U) {
    format = PNG_;
  } else if (magic1 >= 0xFFD8FF00U && magic1 <= 0xFFD8FFFFU) {
    format = JPEG_;
  } else if (magic1 == 0x52494646 && magic2 == 0x57454250) {
    format = WEBP_;
  }
  return format;
}

int
WebpTransform::_readImage(std::stringstream &input_img)
{
  int ok = 0;

  if (_picture.width == 0 || _picture.height == 0) {
    // If no size specified, try to decode it as PNG/JPEG (as appropriate).
    const InputFileFormat format = _getImageType(input_img);
    if (format == PNG_) {
      if (!_png_dec.init(&input_img)) {
        _png_dec.finalize();
        return 0;
      }
      ok = _png_dec.readImage(&_picture, &_metadata);
    } else if (format == JPEG_) {
      if (!_jpeg_dec.init(&input_img)) {
        _jpeg_dec.finalize();
        return 0;
      }
      ok = _jpeg_dec.readImage(&_picture, &_metadata);
    } else if (format == WEBP_) {
      TS_DEBUG(TAG, "Already webp file. Nothing to be done.");
    }
  }
  if (!ok)
    TS_DEBUG(TAG, "Unsupported image format. Failed to read image.");

  return ok;
}


void
WebpTransform::_allocExtraInfo()
{
  const int mb_w = (_picture.width + 15) / 16;
  const int mb_h = (_picture.height + 15) / 16;
  _picture.extra_info = (uint8_t *)malloc(mb_w * mb_h * sizeof(_picture.extra_info));
}

bool
WebpTransform::init()
{
  MetadataInit(&_metadata);
  WebPMemoryWriterInit(&_writer);
  if (!WebPPictureInit(&_picture) || !WebPConfigInit(&_config)) {
    TS_DEBUG(TAG, "DEBUG! Version mismatch");
    return false;
  }
  WebPPreset preset = WEBP_PRESET_PICTURE;
  if (!WebPConfigPreset(&_config, preset, _config.quality)) {
    TS_DEBUG(TAG, "DEBUG! Could initialize configuration with preset.");
    return false;
  }

  if (!WebPValidateConfig(&_config)) {
    TS_DEBUG(TAG, "DEBUG! Invalid configuration.");
    return false;
  }
  _init = true;
  return true;
}


bool
WebpTransform::transform(std::stringstream &input_img)
{
  if (!_readImage(input_img)) {
    TS_DEBUG(TAG, "Cannot read input picture file .");
    return false;
  }
  _picture.progress_hook = NULL;

  _picture.writer = StreamWriter;
  _picture.custom_ptr = (void *)this;

  if (_picture.extra_info_type > 0) {
    _allocExtraInfo();
  }

  if (!WebPEncode(&_config, &_picture)) {
    TS_DEBUG(TAG, "DEBUG! Cannot encode picture as WebP. Error code: %d (%s)", _picture.error_code,
             WebpTransform::_errors[_picture.error_code].c_str());
    return false;
  }
  return true;
}

void
WebpTransform::finalize()
{
  if (_init) {
    _webpMemoryWriterClear();
    free(_picture.extra_info);
    MetadataFree(&_metadata);
    WebPPictureFree(&_picture);

    _png_dec.finalize();
    _jpeg_dec.finalize();
  }
}

void
WebpTransform::writeImage(const char *data, size_t data_size)
{
  _stream.write(data, data_size);
}
