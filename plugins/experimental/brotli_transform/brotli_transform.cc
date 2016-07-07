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
unsigned int BROTLI_QUALITY = 9;
}

BrotliTransformationPlugin::BrotliTransformationPlugin(Transaction &transaction)
  : TransformationPlugin(transaction, RESPONSE_TRANSFORMATION)
{
  registerHook(HOOK_SEND_RESPONSE_HEADERS);
  brotliCompressed_     = true;
  brotliTransformOut_   = new BrotliTransformOut(this);
  brotliParams_.quality = BROTLI_QUALITY;
}

BrotliTransformationPlugin::~BrotliTransformationPlugin()
{
  delete brotliTransformOut_;
}

void
BrotliTransformationPlugin::handleSendResponseHeaders(Transaction &transaction)
{
  if (brotliCompressed_) {
    TS_DEBUG(TAG, "Set response content-encoding to br.");
    transaction.getClientResponse().getHeaders().set("Content-Encoding", "br");
  }
  transaction.resume();
}

void
BrotliTransformationPlugin::consume(const string &data)
{
  const char *dataPtr = data.c_str();
  brotli::BrotliMemIn brotliIn(dataPtr, data.length());

  if (!brotli::BrotliCompress(brotliParams_, &brotliIn, brotliTransformOut_)) {
    TS_DEBUG(TAG, "brotli compress failed.");
    produce(data);
    brotliCompressed_ = false;
  }
}

void
BrotliTransformationPlugin::transformProduce(const string &data)
{
  produce(data);
}

void
BrotliTransformationPlugin::handleInputComplete()
{
  setOutputComplete();
}

void
GlobalHookPlugin::handleReadResponseHeaders(Transaction &transaction)
{
  if (isBrotliSupported(transaction)) {
    TS_DEBUG(TAG, "Brotli is supported.");
    checkContentEncoding(transaction);
    if (osContentEncoding_ == GZIP || osContentEncoding_ == PLAINTEXT) {
      if (osContentEncoding_ == GZIP) {
        TS_DEBUG(TAG, "Origin server return gzip, do gzip inflate.");
        transaction.addPlugin(new GzipInflateTransformation(transaction, TransformationPlugin::RESPONSE_TRANSFORMATION));
      }
      transaction.addPlugin(new BrotliTransformationPlugin(transaction));
    }
  }
  transaction.resume();
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
    osContentEncoding_ = PLAINTEXT;
  } else {
    if (contentEncoding.find("gzip") != string::npos) {
      osContentEncoding_ = GZIP;
    } else {
      osContentEncoding_ = OTHERENCODE;
    }
  }
}

void
TSPluginInit(int argc, const char *argv[])
{
  RegisterGlobalPlugin("CPP_Brotli_Transform", "apache", "dev@trafficserver.apache.org");
  TS_DEBUG(TAG, "TSPluginInit");
  new GlobalHookPlugin();
}
