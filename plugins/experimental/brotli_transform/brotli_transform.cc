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

#include "brotli_transform.h"
#include "brotli_transform_out.h"
#define TAG "brotli_transformation"

namespace
{
unsigned int BROTLI_QUALITY;
vector<string> BLACKLIST_OF_COMPRESS_FILE_TYPE;
}

BrotliTransformationPlugin::BrotliTransformationPlugin(Transaction &transaction)
  : TransformationPlugin(transaction, RESPONSE_TRANSFORMATION)
{
  registerHook(HOOK_READ_RESPONSE_HEADERS);
}

void
BrotliTransformationPlugin::handleReadResponseHeaders(Transaction &transaction)
{
  string contentEncoding = "Content-Encoding";
  Headers &hdr           = transaction.getServerResponse().getHeaders();
  TS_DEBUG(TAG, "Set server response content-encoding to br for url %s.",
           transaction.getClientRequest().getUrl().getUrlString().c_str());
  hdr.set(contentEncoding, "br");
  transaction.resume();
}

void
BrotliTransformationPlugin::consume(const string &data)
{
  buffer_.append(data);
}

void
BrotliTransformationPlugin::transformProduce(const string &data)
{
  produce(data);
}

void
BrotliTransformationPlugin::handleInputComplete()
{
  brotli::BrotliParams params;
  params.quality = BROTLI_QUALITY;

  const char *dataPtr = buffer_.c_str();
  brotli::BrotliMemIn brotliIn(dataPtr, buffer_.length());
  BrotliTransformOut brotliTransformOut(this);

  if (!brotli::BrotliCompress(params, &brotliIn, &brotliTransformOut)) {
    TS_ERROR(TAG, "brotli compress failed.");
    produce(buffer_);
  }
  setOutputComplete();
}

void
GlobalHookPlugin::handleReadResponseHeaders(Transaction &transaction)
{
  if (isBrotliSupported(transaction)) {
    TS_DEBUG(TAG, "Brotli is supported.");
    if (!inCompressBlacklist(transaction)) {
      checkContentEncoding(transaction);
      if (osContentEncoding_ == GZIP || osContentEncoding_ == NONENCODE) {
        if (osContentEncoding_ == GZIP) {
          TS_DEBUG(TAG, "Origin server return gzip, do gzip inflate.");
          transaction.addPlugin(new GzipInflateTransformation(transaction, TransformationPlugin::RESPONSE_TRANSFORMATION));
        }
        transaction.addPlugin(new BrotliTransformationPlugin(transaction));
      }
    }
  }
  transaction.resume();
}

bool
GlobalHookPlugin::inCompressBlacklist(Transaction &transaction)
{
  Headers &hdr       = transaction.getServerResponse().getHeaders();
  string contentType = hdr.values("Content-Type");
  for (vector<string>::iterator it = BLACKLIST_OF_COMPRESS_FILE_TYPE.begin(); it != BLACKLIST_OF_COMPRESS_FILE_TYPE.end(); it++) {
    if (contentType.find(*it) != string::npos) {
      TS_DEBUG(TAG, "Do not compress for url %s", transaction.getClientRequest().getUrl().getUrlString().c_str());
      return true;
    }
  }
  return false;
}

bool
GlobalHookPlugin::isBrotliSupported(Transaction &transaction)
{
  Headers &clientRequestHeaders = transaction.getClientRequest().getHeaders();
  string acceptEncoding         = clientRequestHeaders.values("Accept-Encoding");
  if (acceptEncoding.find("br") != string::npos) {
    return true;
  }
  return false;
}

void
GlobalHookPlugin::checkContentEncoding(Transaction &transaction)
{
  Headers &hdr           = transaction.getServerResponse().getHeaders();
  string contentEncoding = hdr.values("Content-Encoding");
  if (contentEncoding.empty()) {
    osContentEncoding_ = NONENCODE;
  } else {
    if (contentEncoding.find("gzip") != string::npos) {
      osContentEncoding_ = GZIP;
    } else {
      osContentEncoding_ = OTHERENCODE;
    }
  }
}

static void
brotliPluginInit(int argc, const char *argv[])
{
  if (argc > 1) {
    int c;
    static const struct option longopts[] = {
      {const_cast<char *>("quality"), required_argument, NULL, 'q'},
      {const_cast<char *>("compress-files-type-blacklist"), required_argument, NULL, 't'},
      {NULL, 0, NULL, 0},
    };

    int longindex = 0;
    while ((c = getopt_long(argc, (char *const *)argv, "qt:", longopts, &longindex)) != -1) {
      switch (c) {
      case 'q': {
        BROTLI_QUALITY = atoi(optarg);
        TS_DEBUG(TAG, "compress quality is: %d", BROTLI_QUALITY);
        break;
      }
      case 't': {
        TS_DEBUG(TAG, "blacklist of compress file type is:[%s]", optarg);
        stringstream ss(optarg);
        string oneType;
        while (getline(ss, oneType, ',')) {
          BLACKLIST_OF_COMPRESS_FILE_TYPE.push_back(oneType);
        }
        break;
      }
      default:
        break;
      }
    }
  } else {
    TS_DEBUG(TAG, "Set default value of compress quality (9) and file type blacklist (image)");
    BROTLI_QUALITY = 9;
    BLACKLIST_OF_COMPRESS_FILE_TYPE.push_back("image");
  }
}

void
TSPluginInit(int argc, const char *argv[])
{
  RegisterGlobalPlugin("CPP_Brotli_Transform", "apache", "dev@trafficserver.apache.org");
  TS_DEBUG(TAG, "TSPluginInit");
  brotliPluginInit(argc, argv);
  new GlobalHookPlugin();
}
