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
#include <atscppapi/GlobalPlugin.h>
#include <atscppapi/TransactionPlugin.h>
#include <atscppapi/TransformationPlugin.h>
#include <atscppapi/GzipInflateTransformation.h>
#include <atscppapi/GzipDeflateTransformation.h>
#include <atscppapi/PluginInit.h>
#include <atscppapi/Logger.h>

using namespace atscppapi;
using namespace atscppapi::transformations;
using std::string;

#define TAG "gzip_transformation"

/*
 * Note, the GzipInflateTransformation and GzipDeflateTransformation do not
 * check headers to determine if the content was gziped and it doesn't check
 * headers to make sure the client supports gzip, this is entirely up to the plugin
 * to verify that the content-encoding is gzipped, it's also up to the client
 * to make sure the user's accept-encoding supports gzip.
 *
 * Read this example very carefully, in this example we modify the Accept-Encoding
 * header to the origin to ensure that we will be returned gzipped or identity encoding
 * content. If we receive gzipped content we will inflate it, we will apply our transformation
 * and if the client supports gzip we will deflate the content that we transformed. Finally,
 * if the user supported gzip (then they got gzip) and we must make sure the content-encoding
 * header is correctly set on the way out.
 */

class Helpers
{
public:
  static bool
  clientAcceptsGzip(Transaction &transaction)
  {
    return transaction.getClientRequest().getHeaders().values("Accept-Encoding").find("gzip") != string::npos;
  }

  static bool
  serverReturnedGzip(Transaction &transaction)
  {
    return transaction.getServerResponse().getHeaders().values("Content-Encoding").find("gzip") != string::npos;
  }

  enum ContentType {
    UNKNOWN    = 0,
    TEXT_HTML  = 1,
    TEXT_PLAIN = 2,
  };

  static ContentType
  getContentType(Transaction &transaction)
  {
    if (transaction.getServerResponse().getHeaders().values("Content-Type").find("text/html") != string::npos) {
      return TEXT_HTML;
    } else if (transaction.getServerResponse().getHeaders().values("Content-Type").find("text/plain") != string::npos) {
      return TEXT_PLAIN;
    } else {
      return UNKNOWN;
    }
  }
};

class SomeTransformationPlugin : public TransformationPlugin
{
public:
  SomeTransformationPlugin(Transaction &transaction)
    : TransformationPlugin(transaction, RESPONSE_TRANSFORMATION), transaction_(transaction)
  {
    registerHook(HOOK_SEND_RESPONSE_HEADERS);
  }

  void
  handleSendResponseHeaders(Transaction &transaction)
  {
    TS_DEBUG(TAG, "Added X-Content-Transformed header");
    transaction.getClientResponse().getHeaders()["X-Content-Transformed"] = "1";
    transaction.resume();
  }

  void
  consume(const string &data)
  {
    produce(data);
  }

  void
  handleInputComplete()
  {
    Helpers::ContentType content_type = Helpers::getContentType(transaction_);
    if (content_type == Helpers::TEXT_HTML) {
      TS_DEBUG(TAG, "Adding an HTML comment at the end of the page");
      produce("\n<br /><!-- Gzip Transformation Plugin Was Here -->");
    } else if (content_type == Helpers::TEXT_PLAIN) {
      TS_DEBUG(TAG, "Adding a text comment at the end of the page");
      produce("\nGzip Transformation Plugin Was Here");
    } else {
      TS_DEBUG(TAG, "Unable to add TEXT or HTML comment because content type was not text/html or text/plain.");
    }
    setOutputComplete();
  }

  virtual ~SomeTransformationPlugin() {}
private:
  Transaction &transaction_;
};

class GlobalHookPlugin : public GlobalPlugin
{
public:
  GlobalHookPlugin()
  {
    registerHook(HOOK_SEND_REQUEST_HEADERS);
    registerHook(HOOK_READ_RESPONSE_HEADERS);
    registerHook(HOOK_SEND_RESPONSE_HEADERS);
  }

  virtual void
  handleSendRequestHeaders(Transaction &transaction)
  {
    // Since we can only decompress gzip we will change the accept encoding header
    // to gzip, even if the user cannot accept gziped content we will return to them
    // uncompressed content in that case since we have to be able to transform the content.
    string original_accept_encoding = transaction.getServerRequest().getHeaders().values("Accept-Encoding");

    // Make sure it's done on the server request to avoid clobbering the clients original accept encoding header.
    transaction.getServerRequest().getHeaders()["Accept-Encoding"] = "gzip";
    TS_DEBUG(TAG, "Changed the servers request accept encoding header from \"%s\" to gzip", original_accept_encoding.c_str());

    transaction.resume();
  }

  virtual void
  handleReadResponseHeaders(Transaction &transaction)
  {
    TS_DEBUG(TAG, "Determining if we need to add an inflate transformation or a deflate transformation..");
    // We're guaranteed to have been returned either gzipped content or Identity.

    if (Helpers::serverReturnedGzip(transaction)) {
      // If the returned content was gziped we will inflate it so we can transform it.
      TS_DEBUG(TAG, "Creating Inflate Transformation because the server returned gziped content");
      transaction.addPlugin(new GzipInflateTransformation(transaction, TransformationPlugin::RESPONSE_TRANSFORMATION));
    }

    transaction.addPlugin(new SomeTransformationPlugin(transaction));

    // Even if the server didn't return gziped content, if the user supports it we will gzip it.
    if (Helpers::clientAcceptsGzip(transaction)) {
      TS_DEBUG(TAG, "The client supports gzip so we will deflate the content on the way out.");
      transaction.addPlugin(new GzipDeflateTransformation(transaction, TransformationPlugin::RESPONSE_TRANSFORMATION));
    }
    transaction.resume();
  }

  virtual void
  handleSendResponseHeaders(Transaction &transaction)
  {
    // If the client supported gzip then we can guarantee they are receiving gzip since regardless of the
    // origins content-encoding we returned gzip, so let's make sure the content-encoding header is correctly
    // set to gzip or identity.
    if (Helpers::clientAcceptsGzip(transaction)) {
      TS_DEBUG(TAG, "Setting the client response content-encoding to gzip since the user supported it, that's what they got.");
      transaction.getClientResponse().getHeaders()["Content-Encoding"] = "gzip";
    } else {
      TS_DEBUG(TAG, "Setting the client response content-encoding to identity since the user didn't support gzip");
      transaction.getClientResponse().getHeaders()["Content-Encoding"] = "identity";
    }
    transaction.resume();
  }
};

void
TSPluginInit(int argc ATSCPPAPI_UNUSED, const char *argv[] ATSCPPAPI_UNUSED)
{
  RegisterGlobalPlugin("CPP_Example_GzipTransformation", "apache", "dev@trafficserver.apache.org");
  TS_DEBUG(TAG, "TSPluginInit");
  new GlobalHookPlugin();
}
