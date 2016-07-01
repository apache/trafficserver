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
#include <vector>
#include <zlib.h>
#include <atscppapi/GlobalPlugin.h>
#include <atscppapi/TransformationPlugin.h>
#include <atscppapi/GzipInflateTransformation.h>
#include <atscppapi/PluginInit.h>
#include <atscppapi/Logger.h>
#include <brotli/enc/encode.h>

using namespace atscppapi;
using namespace atscppapi::transformations;
using namespace std;

#define TAG "brotli_transformation"

namespace
{
unsigned int BROTLI_QUALITY = 9;
}

class BrotliVecOut : public brotli::BrotliOut
{
public:
  BrotliVecOut(vector<char> &out) : outVec(out) {}
  bool
  Write(const void *buf, size_t n)
  {
    outVec.insert(outVec.end(), (char *)buf, (char *)buf + n);
    return true;
  }

private:
  vector<char> &outVec;
};

class BrotliTransformationPlugin : public TransformationPlugin
{
public:
  BrotliTransformationPlugin(Transaction &transaction) : TransformationPlugin(transaction, RESPONSE_TRANSFORMATION)
  {
    registerHook(HOOK_SEND_RESPONSE_HEADERS);
  }

  void
  handleSendResponseHeaders(Transaction &transaction)
  {
    if (brotliCompressed_) {
      TS_DEBUG(TAG, "Set response content-encoding to br.");
      transaction.getClientResponse().getHeaders().set("Content-Encoding", "br");
    }
    transaction.resume();
  }

  void
  consume(const string &data)
  {
    buffer_.append(data);
  }

  void
  handleInputComplete()
  {
    TS_DEBUG(TAG, "BrotliTransformationPlugin handle Input Complete.");
    brotli::BrotliParams params;
    params.quality = BROTLI_QUALITY;

    const char *dataPtr = buffer_.c_str();
    brotli::BrotliMemIn brotliIn(dataPtr, buffer_.length());
    vector<char> out;
    BrotliVecOut brotliOut(out);
    if (!brotli::BrotliCompress(params, &brotliIn, &brotliOut)) {
      TS_DEBUG(TAG, "brotli compress failed.");
      outputData_ = buffer_;
    } else {
      outputData_       = string(out.begin(), out.end());
      brotliCompressed_ = true;
    }

    produce(outputData_);
    setOutputComplete();
  }

  virtual ~BrotliTransformationPlugin() {}
private:
  string buffer_;
  string outputData_;
  bool brotliCompressed_;
};

class GlobalHookPlugin : public GlobalPlugin
{
public:
  GlobalHookPlugin() { registerHook(HOOK_READ_RESPONSE_HEADERS); }
  void
  handleReadResponseHeaders(Transaction &transaction)
  {
    if (isBrotliSupported(transaction)) {
      TS_DEBUG(TAG, "Brotli is supported.");
      checkContentEncoding(transaction);
      if (osContentEncoding_ == GZIP) {
        transaction.addPlugin(new GzipInflateTransformation(transaction, TransformationPlugin::RESPONSE_TRANSFORMATION));
      }
      if (osContentEncoding_ != NOTSUPPORTED) {
        transaction.addPlugin(new BrotliTransformationPlugin(transaction));
      }
    }
    transaction.resume();
  }

private:
  bool
  isBrotliSupported(Transaction &transaction)
  {
    Headers &clientRequestHeaders = transaction.getClientRequest().getHeaders();
    string acceptEncoding         = clientRequestHeaders.values("Accept-Encoding");
    if (acceptEncoding.find("br") != string::npos) {
      return true;
    }
    return false;
  }

  void
  checkContentEncoding(Transaction &transaction)
  {
    Headers &hdr           = transaction.getServerResponse().getHeaders();
    string contentEncoding = hdr.values("Content-Encoding");
    if (contentEncoding.empty()) {
      osContentEncoding_ = PLAINTEXT;
    } else {
      if (contentEncoding.find("gzip") != string::npos) {
        osContentEncoding_ = GZIP;
      } else {
        osContentEncoding_ = NOTSUPPORTED;
      }
    }
  }

  enum ContentEncoding { GZIP, PLAINTEXT, NOTSUPPORTED };
  ContentEncoding osContentEncoding_;
};

void
TSPluginInit(int argc, const char *argv[])
{
  RegisterGlobalPlugin("CPP_Brotli_Transform", "apache", "dev@trafficserver.apache.org");
  TS_DEBUG(TAG, "TSPluginInit");
  new GlobalHookPlugin();
}
