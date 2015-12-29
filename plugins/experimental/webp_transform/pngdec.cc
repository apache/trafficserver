/**
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

#include <iostream>
#include <sstream>
#include <stdio.h>
#include <png.h>
#include <setjmp.h> // note: this must be included *after* png.h
#include <stdlib.h>
#include <string.h>
#include <atscppapi/Logger.h>

#include "webp/encode.h"
#include "metadata.h"
#include "pngdec.h"
#include "Common.h"

void
PngDec::_errorFunction(png_structp png, png_const_charp error)
{
  if (error != NULL)
    TS_DEBUG("img_transform_png", "libpng error: %s\n", error);
  longjmp(png_jmpbuf(png), 1);
}

void
PngDec::_readFunction(png_structp pngPtr, png_bytep data, png_size_t length)
{
  PngDec *png_dec = reinterpret_cast<PngDec *>(png_get_io_ptr(pngPtr));
  png_dec->_readData(data, length);
}

PngDec::PNGMetadataMap PngDec::_png_metadata_map[] = {
  // http://www.sno.phy.queensu.ca/~phil/exiftool/TagNames/PNG.html#TextualData
  // See also: ExifTool on CPAN.
  {"Raw profile type exif", PngDec::_processRawProfile, METADATA_OFFSET(exif)},
  {"Raw profile type xmp", PngDec::_processRawProfile, METADATA_OFFSET(xmp)},
  // Exiftool puts exif data in APP1 chunk, too.
  {"Raw profile type APP1", PngDec::_processRawProfile, METADATA_OFFSET(exif)},
  // XMP Specification Part 3, Section 3 #PNG
  {"XML:com.adobe.xmp", MetadataCopy, METADATA_OFFSET(xmp)},
  {NULL, NULL, 0},
};

int
PngDec::_processRawProfile(const char *profile, size_t profile_len, MetadataPayload *const payload)
{
  const char *src = profile;
  char *end;
  int expected_length;

  if (profile == NULL || profile_len == 0)
    return 0;

  // ImageMagick formats 'raw profiles' as
  // '\n<name>\n<length>(%8lu)\n<hex payload>\n'.
  if (*src != '\n') {
    TS_DEBUG(TAG, "Malformed raw profile, expected '\\n' got '\\x%.2X'\n", *src);
    return 0;
  }
  ++src;
  // skip the profile name and extract the length.
  while (*src != '\0' && *src++ != '\n') {
  }
  expected_length = (int)strtol(src, &end, 10);
  if (*end != '\n') {
    TS_DEBUG(TAG, "Malformed raw profile, expected '\\n' got '\\x%.2X'\n", *end);
    return 0;
  }
  ++end;

  // 'end' now points to the profile payload.
  payload->bytes = _hexStringToBytes(end, expected_length);
  if (payload->bytes == NULL) {
    TS_DEBUG(TAG, " failed");
    return 0;
  }
  payload->size = expected_length;
  return 1;
}

// Converts the NULL terminated 'hexstring' which contains 2-byte character
// representations of hex values to raw data.
// 'hexstring' may contain values consisting of [A-F][a-f][0-9] in pairs,
// e.g., 7af2..., separated by any number of newlines.
// 'expected_length' is the anticipated processed size.
// On success the raw buffer is returned with its length equivalent to
// 'expected_length'. NULL is returned if the processed length is less than
// 'expected_length' or any character aside from those above is encountered.
// The returned buffer must be freed by the caller.
uint8_t *
PngDec::_hexStringToBytes(const char *hexstring, size_t expected_length)
{
  const char *src = hexstring;
  size_t actual_length = 0;
  uint8_t *const raw_data = (uint8_t *)malloc(expected_length);
  uint8_t *dst;

  if (raw_data == NULL)
    return NULL;

  for (dst = raw_data; actual_length < expected_length && *src != '\0'; ++src) {
    char *end;
    char val[3];
    if (*src == '\n')
      continue;
    val[0] = *src++;
    val[1] = *src;
    val[2] = '\0';
    *dst++ = (uint8_t)strtol(val, &end, 16);
    if (end != val + 2)
      break;
    ++actual_length;
  }
  if (actual_length != expected_length) {
    free(raw_data);
    return NULL;
  }
  return raw_data;
}


// Looks for metadata at both the beginning and end of the PNG file, giving
// preference to the head.
// Returns true on success. The caller must use MetadataFree() on 'metadata' in
// all cases.
int
PngDec::_extractMetadataFromPNG(Metadata *const metadata)
{
  int p;
  for (p = 0; p < 2; ++p) {
    png_infop const info = (p == 0) ? _info : _end_info;
    png_textp text = NULL;
    const int num = png_get_text(_png, info, &text, NULL);
    int i;
    // Look for EXIF / XMP metadata.
    for (i = 0; i < num; ++i, ++text) {
      int j;
      for (j = 0; _png_metadata_map[j].name != NULL; ++j) {
        if (!strcmp(text->key, _png_metadata_map[j].name)) {
          MetadataPayload *const payload = (MetadataPayload *)((uint8_t *)metadata + _png_metadata_map[j].storage_offset);
          png_size_t text_length;
          switch (text->compression) {
#ifdef PNG_iTXt_SUPPORTED
          case PNG_ITXT_COMPRESSION_NONE:
          case PNG_ITXT_COMPRESSION_zTXt:
            text_length = text->itxt_length;
            break;
#endif
          case PNG_TEXT_COMPRESSION_NONE:
          case PNG_TEXT_COMPRESSION_zTXt:
          default:
            text_length = text->text_length;
            break;
          }
          if (payload->bytes != NULL) {
            TS_DEBUG(TAG, "Ignoring additional '%s'\n", text->key);
          } else if (!_png_metadata_map[j].process(text->text, text_length, payload)) {
            TS_DEBUG(TAG, "Failed to process: '%s'\n", text->key);
            return 0;
          }
          break;
        }
      }
    }
    // Look for an ICC profile.
    {
      png_charp name;
      int comp_type;
#if ((PNG_LIBPNG_VER_MAJOR << 8) | PNG_LIBPNG_VER_MINOR << 0) < ((1 << 8) | (5 << 0))
      png_charp profile;
#else // >= libpng 1.5.0
      png_bytep profile;
#endif
      png_uint_32 len;

      if (png_get_iCCP(_png, info, &name, &comp_type, &profile, &len) == PNG_INFO_iCCP) {
        if (!MetadataCopy((const char *)profile, len, &metadata->iccp))
          return 0;
      }
    }
  }

  return 1;
}

bool
PngDec::init(std::stringstream *img)
{
  _input_img = img;
  _png = png_create_read_struct(PNG_LIBPNG_VER_STRING, 0, 0, 0);
  if (_png == NULL) {
    TS_DEBUG(TAG, "Error! Unable to create read structure");
    return false;
  }

  png_set_error_fn(_png, 0, PngDec::_errorFunction, NULL);
  if (setjmp(png_jmpbuf(_png))) {
    TS_DEBUG(TAG, "Error! setjmp failed");
    return false;
  }

  _info = png_create_info_struct(_png);
  if (_info == NULL) {
    TS_DEBUG(TAG, "Error! could not create info struct for info_");
    return false;
  }
  _end_info = png_create_info_struct(_png);
  if (_end_info == NULL) {
    TS_DEBUG(TAG, "Error! could not create info struct for info_");
    return false;
  }

  // png_init_io(png, in_file);
  png_set_read_fn(_png, (void *)this, PngDec::_readFunction);
  png_read_info(_png, _info);
  _init = true;
  return true;
}

void
PngDec::finalize()
{
  if (_init && _png != NULL) {
    png_destroy_read_struct((png_structpp)&_png, (png_infopp)&_info, (png_infopp)&_end_info);
    _png = NULL;
    _info = _end_info = NULL;
  }
}

int
PngDec::readImage(WebPPicture *const pic, Metadata *const metadata)
{
  int color_type, bit_depth, interlaced;
  int has_alpha;
  int num_passes;
  int p;
  int ok = 0;
  png_uint_32 width, height, y;
  int stride;
  uint8_t *volatile rgb = NULL;


  if (!png_get_IHDR(_png, _info, &width, &height, &bit_depth, &color_type, &interlaced, NULL, NULL)) {
    TS_DEBUG(TAG, "failed to get IHDR");
    return false;
  }

  png_set_strip_16(_png);
  png_set_packing(_png);
  if (color_type == PNG_COLOR_TYPE_PALETTE) {
    png_set_palette_to_rgb(_png);
  }
  if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
    if (bit_depth < 8) {
      png_set_expand_gray_1_2_4_to_8(_png);
    }
    png_set_gray_to_rgb(_png);
  }
  if (png_get_valid(_png, _info, PNG_INFO_tRNS)) {
    png_set_tRNS_to_alpha(_png);
    has_alpha = 1;
  } else {
    has_alpha = !!(color_type & PNG_COLOR_MASK_ALPHA);
  }

  num_passes = png_set_interlace_handling(_png);
  png_read_update_info(_png, _info);
  stride = (has_alpha ? 4 : 3) * width * sizeof(*rgb);
  rgb = (uint8_t *)malloc(stride * height);
  if (rgb == NULL)
    return false;
  for (p = 0; p < num_passes; ++p) {
    for (y = 0; y < height; ++y) {
      png_bytep row = (png_bytep)(rgb + y * stride);
      png_read_rows(_png, &row, NULL, 1);
    }
  }
  png_read_end(_png, _end_info);

  if (metadata != NULL && !_extractMetadataFromPNG(metadata)) {
    TS_DEBUG(TAG, "Error!! extracting PNG metadata!");
    free(rgb);
    return false;
  }

  pic->width = width;
  pic->height = height;
  pic->use_argb = 1;
  ok = has_alpha ? WebPPictureImportRGBA(pic, rgb, stride) : WebPPictureImportRGB(pic, rgb, stride);

  free(rgb);
  return ok;
}


bool
PngDec::_readData(png_bytep data, png_size_t length)
{
  _input_img->read((char *)data, length);
  return true;
}
