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

#include "ts/apidefs.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <iterator>
#include <list>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <cassert>
#include <cstring>

#include <tscpp/api/GlobalPlugin.h>
#include <tscpp/api/PluginInit.h>
#include <tscpp/api/TransformationPlugin.h>

#include <ts/ts.h>

#include "magick_args.h"

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

#if __has_include(<MagickWand/MagickWand.h>)
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wtype-limits"
#endif
#include <MagickWand/MagickWand.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#else
#include <wand/MagickWand.h>
#endif

#define PLUGIN_TAG "ats-magick"

using std::string;
using namespace atscppapi;

// CharVector and CharPointerVector come from magick_args.h.
using StringViewVector = std::vector<std::string_view>;

using byte = unsigned char;

namespace
{
GlobalPlugin *plugin;

DbgCtl dbg_ctl{PLUGIN_TAG};
} // namespace

struct ThreadPool {
  using Callback = std::function<void(void)>;
  using Queue    = std::list<Callback>;
  using Lock     = std::unique_lock<std::mutex>;

  const size_t    size_;
  bool            running_;
  TSThread *const pool_;

  // a double linked list is used due to the high amount of insertions and deletions
  Queue queue_;

  // This mutex protects operations into the queue
  std::mutex mutex_;

  std::condition_variable semaphore_;

  ~ThreadPool()
  {
    assert(running_);
    running_ = false;

    Lock lock(mutex_);

    queue_.clear();

    semaphore_.notify_all();

    assert(nullptr != pool_);
    for (size_t i = 0; i < size_; ++i) {
      TSThread thread = pool_[i];
      assert(nullptr != thread);
      Dbg(dbg_ctl, "Destroying thread number %lu (%p)", i, thread);
      TSThreadWait(thread);
      TSThreadDestroy(thread);
    }

    delete[] pool_;
    const_cast<TSThread *&>(pool_) = nullptr;
  }

  ThreadPool(const size_t s) : size_(s), running_(true), pool_(new TSThread[s])
  {
    assert(0 < size_);
    assert(nullptr != pool_);
    for (size_t i = 0; i < size_; ++i) {
      pool_[i] = TSThreadCreate(
        [](void *d) -> void * {
          assert(nullptr != d);
          ThreadPool *const self = reinterpret_cast<ThreadPool *>(d);
          assert(self->running_);
          while (self->running_) {
            Callback callback;
            {
              Lock lock(self->mutex_);

              self->semaphore_.wait(lock, [self] { return !self->queue_.empty() || !self->running_; });

              if (!self->queue_.empty()) {
                callback = self->queue_.front();
                self->queue_.pop_front();
              }
            }

            // if a callback was assigned, call it outside of the synchronized scope.
            if (callback) {
              callback();
            }
          }
          return nullptr;
        },
        this);
      assert(nullptr != pool_[i]);
      Dbg(dbg_ctl, "Creating thread number %lu (%p)", i, pool_[i]);
    }
  }

  ThreadPool(ThreadPool &) = delete;

  void
  emplace_back(Callback &&c)
  {
    {
      Lock lock(mutex_);
      /**
       * we check for running_ in case the destructor was simultaneously called
       *from a different thread while waiting.
       */
      if (!running_) {
        return;
      }
      queue_.emplace_back(c);
    }
    semaphore_.notify_one();
  }
};

namespace magick
{
struct EVPContext {
  EVP_MD_CTX *const context;

  ~EVPContext()
  {
    assert(nullptr != context);
    EVP_MD_CTX_free(context);
  }

  EVPContext() : context(EVP_MD_CTX_new()) { assert(nullptr != context); }
};

struct EVPKey {
  EVP_PKEY *const key;

  ~EVPKey()
  {
    assert(nullptr != key);
    EVP_PKEY_free(key);
  }

  EVPKey() : key(EVP_PKEY_new()) { assert(nullptr != key); }

  bool
  assign(RSA *k) const
  {
    assert(nullptr != k);
    const int rc = EVP_PKEY_assign_RSA(key, k);
    assert(1 == rc);
    return 1 == rc;
  }
};

/** Remove the last error from this thread's error queue and print it.
 */
static void
ssl_error()
{
  if (unsigned long error_code{ERR_get_error()}; 0 != error_code) {
    if (char const *reason{ERR_reason_error_string(error_code)}; NULL == reason) {
      Dbg(dbg_ctl, "SSL error: error code %lu", error_code);
    } else {
      Dbg(dbg_ctl, "SSL error: %s", reason);
    }
  }
}

bool
verify(const byte *const msg, const size_t mlen, const byte *const sig, const size_t slen, EVP_PKEY *const pkey)
{
  assert(nullptr != msg);
  assert(0 < mlen);
  assert(nullptr != sig);
  assert(0 < slen);
  assert(nullptr != pkey);

  if (nullptr == msg || 0 == mlen || nullptr == sig || 0 == slen || nullptr == pkey) { // lgtm[cpp/constant-comparison]
    return false;
  }

  EVPContext evp;

  if (const int rc = EVP_DigestVerifyInit(evp.context, nullptr, EVP_sha256(), nullptr, pkey); 1 != rc) {
    ssl_error();
    return false;
  }

  if (const int rc = EVP_DigestVerify(evp.context, sig, slen, msg, mlen); 1 == rc) {
    return true;
  } else {
    // The OpenSSL 1.1.1 API distinguishes between a verification failure and a
    // more serious error with a specific return value for the former, but we
    // have collapsed them into one case because we don't do any special
    // handling for serious errors.
    ssl_error();
    return false;
  }
}

struct Exception {
  ExceptionInfo *info;

  ~Exception()
  {
    assert(nullptr != info);
    info = DestroyExceptionInfo(info);
  }

  Exception() : info(AcquireExceptionInfo()) { assert(nullptr != info); }

  /** The reason ImageMagick recorded, if any, so a failure can be diagnosed from
   * the error log instead of a fixed string.
   */
  std::string
  text() const
  {
    assert(nullptr != info);
    std::string result;

    if (nullptr != info->reason) {
      result += info->reason;
    }
    if (nullptr != info->description) {
      if (!result.empty()) {
        result += ": ";
      }
      result += info->description;
    }

    return result;
  }
};

struct Image {
  ImageInfo *info;

  ~Image()
  {
    assert(nullptr != info);
    info = DestroyImageInfo(info);
  }

  Image() : info(AcquireImageInfo()) { assert(nullptr != info); }
};

struct Wand {
  MagickWand *wand;
  void       *blob = nullptr;

  ~Wand()
  {
    assert(nullptr != wand);
    wand = DestroyMagickWand(wand);
    if (nullptr != blob) {
      blob = MagickRelinquishMemory(blob);
    }
  }

  Wand() : wand(NewMagickWand()) { assert(nullptr != wand); }

  /** The reason the last wand operation failed, so a failure can be diagnosed from
   * the error log instead of a fixed string.
   */
  std::string
  exceptionText() const
  {
    assert(nullptr != wand);
    ExceptionType severity = UndefinedException;
    std::string   result;

    if (char *const description = MagickGetException(wand, &severity); nullptr != description) {
      result = description;
      MagickRelinquishMemory(description);
    }

    return result;
  }

  void
  clear() const
  {
    assert(nullptr != wand);
    ClearMagickWand(wand);
  }

  std::string_view
  get()
  {
    assert(nullptr != wand);
    std::size_t length = 0;
    if (nullptr != blob) {
      blob = MagickRelinquishMemory(blob);
    }
    MagickResetIterator(wand);
    blob = MagickGetImagesBlob(wand, &length);
    return std::string_view(reinterpret_cast<char *>(blob), length);
  }

  bool
  read(const char *const s) const
  {
    assert(nullptr != s);
    assert(nullptr != wand);
    return MagickReadImage(wand, s) == MagickTrue;
  }

  bool
  readBlob(const std::vector<char> &v) const
  {
    assert(!v.empty());
    assert(nullptr != wand);
    return MagickReadImageBlob(wand, v.data(), v.size()) == MagickTrue;
  }

  bool
  setFormat(const char *const s) const
  {
    assert(nullptr != s);
    assert(nullptr != wand);
    return MagickSetImageFormat(wand, s) == MagickTrue;
  }

  bool
  write(const char *const s) const
  {
    assert(nullptr != s);
    assert(nullptr != wand);
    return MagickWriteImage(wand, s) == MagickTrue;
  }
};

struct Core {
  ~Core() { MagickCoreTerminus(); }

  Core() { MagickCoreGenesis("/tmp", MagickFalse); }
};

} // namespace magick

struct QueryMap {
  using Vector = StringViewVector;
  using Map    = std::map<std::string_view, Vector>;

  const static Vector emptyValues;

  std::string content_;
  Map         map_;

  QueryMap(std::string &&s) : content_(std::move(s)) { parse(); }

  template <typename T>
  const Vector &
  operator[](T &&k) const
  {
    const auto iterator = map_.find(k);
    if (iterator != map_.end()) {
      return iterator->second;
    }
    return emptyValues;
  }

  void
  parse()
  {
    std::string_view key;
    std::size_t      i = 0, j = 0;
    for (; i < content_.size(); ++i) {
      const char c = content_[i];
      switch (c) {
      case '&':
        if (!key.empty()) {
          map_[key].emplace_back(std::string_view(&content_[j], i - j));
          key = std::string_view();
        }
        j = i + 1;
        break;
      case '=':
        key = std::string_view(&content_[j], i - j);
        j   = i + 1;
        break;
      default:
        break;
      }
    }

    assert(j <= i);

    if (key.empty()) {
      if (j < i) {
        map_[std::string_view(&content_[j], i - j)];
      }

    } else {
      map_[key].emplace_back(std::string_view(&content_[j], i - j));
    }
  }
};

const QueryMap::Vector QueryMap::emptyValues;

bool
QueryParameterToCharVector(CharVector &v)
{
  {
    std::size_t s = 0;
    // TSStringPercentDecode always reports success, so this cannot currently fail;
    // the check is here so it does the right thing if that ever changes.
    const TSReturnCode rc = TSStringPercentDecode(v.data(), v.size(), v.data(), v.size(), &s);

    if (rc != TS_SUCCESS) {
      return false;
    }
    v.resize(s);
  }

  {
    std::size_t        s  = 0;
    const TSReturnCode rc = TSBase64Decode(v.data(), v.size(), reinterpret_cast<unsigned char *>(v.data()), v.size(), &s);

    // This rejects any input too short to be valid base64, which is client
    // controlled and reachable before signature verification, so it must return a
    // failure rather than assert.
    if (rc != TS_SUCCESS) {
      return false;
    }
    v.resize(s);
  }

  return true;
}

struct ImageTransform : TransformationPlugin {
  ~ImageTransform() override = default;

  ImageTransform(Transaction &t, CharVector &&a, CharPointerVector &&m, ThreadPool &p)
    : TransformationPlugin(t, TransformationPlugin::RESPONSE_TRANSFORMATION),
      arguments_(std::move(a)),
      argumentMap_(std::move(m)),
      threadPool_(p),
      txn_(static_cast<TSHttpTxn>(t.getAtsHandle())),
      // fail() runs on a pool thread, where touching the transaction to read the
      // URL is not safe, so capture it here while still on the transaction thread.
      url_(t.getClientRequest().getUrl().getUrlString())
  {
    Dbg(dbg_ctl, "ImageTransform");
    TransformationPlugin::registerHook(HOOK_SEND_RESPONSE_HEADERS);
  }

  /** A failed conversion must not be served as a 200. The client response headers
   * are not built until setOutputComplete, so the failure recorded by the background
   * thread can still be turned into an error here.
   *
   * This only rewrites the client response. It does not stop the cache write, because
   * the headers destined for the cache were already snapshotted into
   * cache_info.transform_store by HttpTransact::handle_transform_ready, which runs
   * off the first TSVConnWrite that setOutputComplete issues. fail() disables the
   * transformed-response cache write directly for that reason.
   *
   * This runs before the response body is committed only because the success path
   * has exactly one produce() and the failure path has none: SEND_RESPONSE_HDR is
   * gated on the first TSVConnWrite, latched once via TransformVConnection's
   * m_called_user, not on setOutputComplete. A produce()-then-fail() path would
   * commit the 200 header first and silently stop the 502 from working.
   */
  void
  handleSendResponseHeaders(Transaction &t) override
  {
    if (failed_) {
      Response &response = t.getClientResponse();
      response.setStatusCode(HTTP_STATUS_BAD_GATEWAY);
      response.setReasonPhrase("Bad Gateway");
      // This is an empty error response, not an image.
      Headers &headers = response.getHeaders();

      headers.erase("Content-Type");

      // The origin's freshness and validator headers describe an image that was
      // never produced. Leaving them would let a downstream cache or browser store
      // this 502 against the URL.
      headers.erase("Cache-Control");
      headers.erase("Expires");
      headers.erase("ETag");
      headers.erase("Last-Modified");
      headers.erase("Vary");
      headers.set("Cache-Control", "no-store");
    }
    t.resume();
  }

  void
  consume(const std::string_view s) override
  {
    Dbg(dbg_ctl, "consume");
    blob_.insert(blob_.end(), s.begin(), s.end());
  }

  void
  handleInputComplete() override
  {
    Dbg(dbg_ctl, "handleInputComplete");

    threadPool_.emplace_back([this]() {
      magick::Image     image;
      magick::Exception exception;
      magick::Wand      wand;

      // An empty body is origin controlled, so it has to fail rather than assert.
      if (this->blob_.empty()) {
        this->fail("origin returned an empty body");
        return;
      }

      /** Every step is checked. ImageMagick's MPR registry is process-global rather
       * than per-wand, so on a failed conversion mpr:a still holds whatever a
       * previous transform wrote, from any thread. Producing unconditionally would
       * serve that unrelated image under this request's URL, status and
       * Content-Type, and cache it.
       */
      if (!wand.readBlob(this->blob_)) {
        this->fail("failed to decode the origin image", wand.exceptionText());
        return;
      }

      if (!wand.write("mpr:b")) {
        this->fail("failed to write the input MPR register", wand.exceptionText());
        return;
      }

      if (MagickCommandGenesis(image.info, ConvertImageCommand, this->argumentMap_.size(), this->argumentMap_.data(), nullptr,
                               exception.info) != MagickTrue) {
        this->fail("convert command failed", exception.text());
        return;
      }

      wand.clear();

      if (!wand.read("mpr:a")) {
        this->fail("failed to read the output MPR register", wand.exceptionText());
        return;
      }

      const std::string_view output = wand.get();

      if (output.empty()) {
        this->fail("conversion produced an empty image", wand.exceptionText());
        return;
      }

      this->produce(output);

      Dbg(dbg_ctl, "Background transformation is done, resuming continuation (%p)", this);

      this->setOutputComplete();
    });

    Dbg(dbg_ctl, "Scheduling background transformation (%p)", this);
  }

  /** Emit no body, suppress the cache write, and let handleSendResponseHeaders turn
   * the response into a 502.
   *
   * Setting failed_ and disabling the cache write before setOutputComplete() is
   * load-bearing, not incidental ordering. setOutputComplete() is what drives the
   * transform read to completion, and therefore what makes the transaction thread
   * observe both. api_info.cache_transformed is read by
   * HttpTransact::handle_transform_ready, which runs off that same write.
   */
  void
  fail(const char *const reason, const std::string &detail = std::string())
  {
    if (detail.empty()) {
      Dbg(dbg_ctl, "transformation failed (%p): %s [%s]", this, reason, url_.c_str());
      TSError("[" PLUGIN_TAG "] %s: %s", reason, url_.c_str());
    } else {
      Dbg(dbg_ctl, "transformation failed (%p): %s: %s [%s]", this, reason, detail.c_str(), url_.c_str());
      TSError("[" PLUGIN_TAG "] %s: %s: %s", reason, detail.c_str(), url_.c_str());
    }

    failed_ = true;

    TSHttpTxnTransformedRespCache(txn_, 0);

    setOutputComplete();
  }

  CharVector        arguments_;
  CharPointerVector argumentMap_;
  CharVector        blob_;
  ThreadPool       &threadPool_;
  TSHttpTxn         txn_ = nullptr;
  std::string       url_;

  /// Written on a pool thread in fail(), read on an ET_NET thread in
  /// handleSendResponseHeaders().
  std::atomic<bool> failed_ = false;
};

struct GlobalHookPlugin : GlobalPlugin {
  magick::Core    core_;
  magick::EVPKey *key_ = nullptr;
  ThreadPool      threadPool_;

  ~GlobalHookPlugin() override
  {
    if (nullptr != key_) {
      delete key_;
      key_ = nullptr;
    }
  }

  GlobalHookPlugin(const char *const f = nullptr) : threadPool_(2)
  {
    if (nullptr != f) {
      assert(0 < strlen(f));
      Dbg(dbg_ctl, "public key file: %s", f);
      key_             = new magick::EVPKey();
      FILE *const file = fopen(f, "r");
      assert(nullptr != file);
      RSA *rsa = nullptr;
      PEM_read_RSA_PUBKEY(file, &rsa, nullptr, nullptr);
      assert(nullptr != rsa);
      fclose(file);
      key_->assign(rsa);
    }

    registerHook(HOOK_SEND_REQUEST_HEADERS);
    registerHook(HOOK_READ_RESPONSE_HEADERS);
  }

  void
  handleSendRequestHeaders(Transaction &t) override
  {
    Headers &headers = t.getServerRequest().getHeaders();
    // preventing origin from sending the content in a different non expected encoding.
    headers.erase("Accept-Encoding");
    headers.erase("accept-encoding");
    t.resume();
  }

  void
  handleReadResponseHeaders(Transaction &t) override
  {
    Headers &headers = t.getServerResponse().getHeaders();

    string contentType = headers.values("Content-Type");

    if (contentType.empty()) {
      contentType = headers.values("content-type");
    }

    std::transform(contentType.cbegin(), contentType.cend(), contentType.begin(), ::tolower);

    const bool compatibleContentType = "image/bmp" == contentType || "image/gif" == contentType || "image/jpeg" == contentType ||
                                       "image/jpg" == contentType || "image/png" == contentType || "image/tiff" == contentType ||
                                       "image/webp" == contentType || "image/svg+xml" == contentType ||
                                       "application/pdf" == contentType || "application/postscript" == contentType;

    if (compatibleContentType) {
      Dbg(dbg_ctl, "Content-Type is compatible: %s", contentType.c_str());
      const QueryMap queryMap(t.getServerRequest().getUrl().getQuery());
      const auto    &magickQueryParameter = queryMap["magick"];
      if (!magickQueryParameter.empty()) {
        const auto &view = magickQueryParameter.front();
        CharVector  magick(view.data(), view.data() + view.size());

        // Fail closed: a request is only trusted when a signing key is
        // configured AND the accompanying signature verifies.
        bool verified = false;

        if (nullptr != key_) {
          const auto &magickSigQueryParameter = queryMap["magickSig"];
          if (!magickSigQueryParameter.empty()) {
            const auto &view2 = magickSigQueryParameter.front();
            CharVector  magickSig(view2.data(), view2.data() + view2.size());
            magickSig.insert(magickSig.end(), '\0');
            Dbg(dbg_ctl, "Magick Signature: %s", magickSig.data());
            if (QueryParameterToCharVector(magickSig)) {
              verified = magick::verify(reinterpret_cast<const byte *>(magick.data()), magick.size(),
                                        reinterpret_cast<const byte *>(magickSig.data()), magickSig.size(), key_->key);
            }
          }
        }

        if (verified) {
          magick.insert(magick.end(), '\0');
          const bool decoded = QueryParameterToCharVector(magick);

          // Keep the terminator within size() so every argv token, including the
          // last, is a NUL-terminated C string without depending on the decoders
          // leaving one in the vector's unused capacity.
          CharPointerVector argumentMap;
          std::string       reason;

          if (decoded) {
            magick.push_back('\0');
            Dbg(dbg_ctl, "ImageMagick's syntax: %s", magick.data());
            argumentMap = QueryParameterToArguments(magick);
          }

          if (decoded && ValidateMagickArguments(argumentMap, &reason)) {
            t.addPlugin(new ImageTransform(t, std::move(magick), std::move(argumentMap), threadPool_));
          } else {
            Dbg(dbg_ctl, "disallowed ImageMagick operator in magick arguments: %s", reason.c_str());
            TSError("[" PLUGIN_TAG "] disallowed ImageMagick operator in magick arguments: %s", reason.c_str());
            t.setStatusCode(HTTP_STATUS_FORBIDDEN);
            t.error();
            // error() is a terminal reenable; falling through to t.resume() below
            // would reenable the transaction a second time.
            return;
          }
        } else {
          Dbg(dbg_ctl, "signature verification failed.");
          TSError("[" PLUGIN_TAG "] signature verification failed.");
          t.setStatusCode(HTTP_STATUS_FORBIDDEN);
          t.error();
          // error() is a terminal reenable; see above.
          return;
        }
      }
    }

    t.resume();
  }
};

void
TSPluginInit(int argc, const char **argv)
{
  if (!RegisterGlobalPlugin("magick", "netlify", "daniel.morilha@netlify.com")) {
    return;
  }

  // Fail closed: Refuses to load without a key
  if (argc < 2) {
    TSError("[" PLUGIN_TAG "] a public key file argument is required; refusing to load without a signing key");
    Dbg(dbg_ctl, "no public key configured; refusing to load");
    return;
  }

  // first argument is the path to the public key used to verify query parameter magick's content.
  const char *const key = argv[1];

  plugin = new GlobalHookPlugin(key);
}
