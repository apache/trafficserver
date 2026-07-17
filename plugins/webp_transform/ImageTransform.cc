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

#include <sstream>
#include <iostream>
#include <string_view>
#include <utility>

#include "ts/ts.h"

#include "tscpp/api/PluginInit.h"
#include "tscpp/api/GlobalPlugin.h"
#include "tscpp/api/TransformationPlugin.h"
#include "tscpp/api/Logger.h"
#include "tscpp/api/Stat.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#if !defined(__clang__)
#pragma GCC diagnostic ignored "-Wsuggest-override"
#endif
#pragma GCC diagnostic ignored "-Wtype-limits"
#endif
#include <Magick++.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

using namespace Magick;
using namespace atscppapi;

#define TAG "webp_transform"

namespace
{
GlobalPlugin *plugin;

DbgCtl webp_dbg_ctl{TAG};

enum class ImageEncoding { webp, jpeg, png, unknown };

bool config_convert_to_webp = false;
bool config_convert_to_jpeg = false;

Stat stat_convert_to_webp;
Stat stat_convert_to_jpeg;

bool
has_signature_for(std::string_view data, ImageEncoding encoding)
{
  constexpr std::string_view png_signature{"\x89PNG\r\n\x1a\n", 8};

  switch (encoding) {
  case ImageEncoding::webp:
    return data.size() >= 12 && data.substr(0, 4) == "RIFF" && data.substr(8, 4) == "WEBP";
  case ImageEncoding::jpeg:
    return data.size() >= 3 && static_cast<unsigned char>(data[0]) == 0xff && static_cast<unsigned char>(data[1]) == 0xd8 &&
           static_cast<unsigned char>(data[2]) == 0xff;
  case ImageEncoding::png:
    return data.starts_with(png_signature);
  case ImageEncoding::unknown:
    return false;
  }

  return false;
}
} // namespace

class ImageTransform : public TransformationPlugin
{
public:
  ImageTransform(Transaction &transaction, std::string input_content_type, ImageEncoding input_image_type,
                 ImageEncoding transform_image_type)
    : TransformationPlugin(transaction, TransformationPlugin::RESPONSE_TRANSFORMATION),
      _input_content_type(std::move(input_content_type)),
      _input_image_type(input_image_type),
      _transform_image_type(transform_image_type)
  {
    TransformationPlugin::registerHook(HOOK_READ_RESPONSE_HEADERS);
    TransformationPlugin::registerHook(HOOK_SEND_RESPONSE_HEADERS);
  }

  void
  handleReadResponseHeaders(Transaction &transaction) override
  {
    transaction.getServerResponse().getHeaders()["Vary"] = "Accept"; // to have a separate cache entry

    Dbg(webp_dbg_ctl, "url %s", transaction.getServerRequest().getUrl().getUrlString().c_str());
    transaction.resume();
  }

  void
  handleSendResponseHeaders(Transaction &transaction) override
  {
    if (_transform_image_type == _input_image_type) {
      transaction.getClientResponse().getHeaders()["Content-Type"] = _input_content_type;
      transaction.resume();
      return;
    }

    switch (_transform_image_type) {
    case ImageEncoding::webp:
      transaction.getClientResponse().getHeaders()["Content-Type"] = "image/webp";
      break;
    case ImageEncoding::jpeg:
      transaction.getClientResponse().getHeaders()["Content-Type"] = "image/jpeg";
      break;
    case ImageEncoding::png:
      transaction.getClientResponse().getHeaders()["Content-Type"] = "image/png";
      break;
    case ImageEncoding::unknown:
      // do nothing
      break;
    }

    transaction.resume();
  }

  void
  consume(std::string_view data) override
  {
    _img.write(data.data(), data.length());
  }

  void
  handleInputComplete() override
  {
    std::string input_data = _img.str();

    if (!has_signature_for(input_data, _input_image_type)) {
      TSError("[webp_transform] input body does not match its declared image encoding: %d, length: %zu",
              static_cast<int>(_input_image_type), input_data.length());
      pass_through(input_data);
      setOutputComplete();
      return;
    }

    Blob  input_blob(input_data.data(), input_data.length());
    Image image;

    try {
      image.read(input_blob);

      Blob output_blob;
      if (_transform_image_type == ImageEncoding::webp) {
        stat_convert_to_webp.increment(1);
        Dbg(webp_dbg_ctl, "Transforming jpeg or png to webp");
        image.magick("WEBP");
      } else {
        stat_convert_to_jpeg.increment(1);
        Dbg(webp_dbg_ctl, "Transforming webp to jpeg");
        image.magick("JPEG");
      }
      image.write(&output_blob);
      produce(std::string_view(reinterpret_cast<const char *>(output_blob.data()), output_blob.length()));
    } catch (Magick::Warning &warning) {
      TSError("ImageMagick++ warning: %s", warning.what());
      pass_through(std::string_view(reinterpret_cast<const char *>(input_blob.data()), input_blob.length()));
    } catch (Magick::Error &error) {
      TSError("ImageMagick++ error: %s _image_type: %d input_data.length(): %zu", error.what(), (int)_transform_image_type,
              input_data.length());
      pass_through(std::string_view(reinterpret_cast<const char *>(input_blob.data()), input_blob.length()));
    }

    setOutputComplete();
  }

  ~ImageTransform() override = default;

private:
  void
  pass_through(std::string_view data)
  {
    if (!data.empty()) {
      produce(data);
    }
    _transform_image_type = _input_image_type;
  }

  std::stringstream _img;
  std::string       _input_content_type;
  ImageEncoding     _input_image_type;
  ImageEncoding     _transform_image_type;
};

class GlobalHookPlugin : public GlobalPlugin
{
public:
  GlobalHookPlugin() { registerHook(HOOK_READ_RESPONSE_HEADERS); }
  void
  handleReadResponseHeaders(Transaction &transaction) override
  {
    // This variable stores the incoming image type
    ImageEncoding input_image_type = ImageEncoding::unknown;

    // This method tries to optimize the amount of string searching at the expense of double checking some of the booleans

    std::string ctype = transaction.getServerResponse().getHeaders().values("Content-Type");

    // Test to if in this transaction we might want to convert jpeg or png to webp
    bool transaction_convert_to_webp = false;
    if (config_convert_to_webp == true) {
      if (ctype.find("image/jpeg") != std::string::npos) {
        input_image_type            = ImageEncoding::jpeg;
        transaction_convert_to_webp = true;
      }
      if (ctype.find("image/png") != std::string::npos) {
        input_image_type            = ImageEncoding::png;
        transaction_convert_to_webp = true;
      }
    }

    // Test to if in this transaction we might want to convert webp to jpeg
    bool transaction_convert_to_jpeg = false;
    if (config_convert_to_jpeg == true && transaction_convert_to_webp == false) {
      transaction_convert_to_jpeg = ctype.find("image/webp") != std::string::npos;
      if (transaction_convert_to_jpeg) {
        input_image_type = ImageEncoding::webp;
      }
    }

    Dbg(webp_dbg_ctl, "Content-Type: %s transaction_convert_to_webp: %d transaction_convert_to_jpeg: %d", ctype.c_str(),
        transaction_convert_to_webp, transaction_convert_to_jpeg);

    // If we might need to convert check to see if what the browser supports
    if (transaction_convert_to_webp == true || transaction_convert_to_jpeg == true) {
      std::string accept         = transaction.getServerRequest().getHeaders().values("Accept");
      bool        webp_supported = accept.find("image/webp") != std::string::npos;
      Dbg(webp_dbg_ctl, "Accept: %s webp_suppported: %d", accept.c_str(), webp_supported);

      if (webp_supported == true && transaction_convert_to_webp == true) {
        Dbg(webp_dbg_ctl, "Content type is either jpeg or png. Converting to webp");
        transaction.addPlugin(new ImageTransform(transaction, ctype, input_image_type, ImageEncoding::webp));
      } else if (webp_supported == false && transaction_convert_to_jpeg == true) {
        Dbg(webp_dbg_ctl, "Content type is webp. Converting to jpeg");
        transaction.addPlugin(new ImageTransform(transaction, ctype, input_image_type, ImageEncoding::jpeg));
      } else {
        Dbg(webp_dbg_ctl, "Nothing to convert");
      }
    }

    transaction.resume();
  }
};

void
TSPluginInit(int argc, const char *argv[])
{
  if (!RegisterGlobalPlugin("CPP_Webp_Transform", "apache", "dev@trafficserver.apache.org")) {
    return;
  }

  if (argc >= 2) {
    std::string option(argv[1]);
    if (option.find("convert_to_webp") != std::string::npos) {
      Dbg(webp_dbg_ctl, "Configured to convert to webp");
      config_convert_to_webp = true;
    }
    if (option.find("convert_to_jpeg") != std::string::npos) {
      Dbg(webp_dbg_ctl, "Configured to convert to jpeg");
      config_convert_to_jpeg = true;
    }
    if (config_convert_to_webp == false && config_convert_to_jpeg == false) {
      TSError("Unknown option: %s", option.c_str());
    }
  } else {
    Dbg(webp_dbg_ctl, "Default configuration is to convert both webp and jpeg");
    config_convert_to_webp = true;
    config_convert_to_jpeg = true;
  }

  stat_convert_to_webp.init("plugin." TAG ".convert_to_webp", Stat::SYNC_SUM, false);
  stat_convert_to_jpeg.init("plugin." TAG ".convert_to_jpeg", Stat::SYNC_SUM, false);

  InitializeMagick("");
  plugin = new GlobalHookPlugin();
}
