/**
  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or ageed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <list>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <cassert>
#include <cstring>

#include <tscpp/api/GlobalPlugin.h>
#include <tscpp/api/PluginInit.h>
#include <tscpp/api/TransformationPlugin.h>

#include <ts/ts.h>

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

#if MAGICK_VERSION > 6
#include <MagickWand/MagickWand.h>
#else
#include <wand/MagickWand.h>
#endif

#define PLUGIN_TAG "ats-magick"

using std::string;
using namespace atscppapi;

using CharVector        = std::vector<char>;
using CharPointerVector = std::vector<char *>;
using StringViewVector  = std::vector<std::string_view>;

using byte = unsigned char;

struct ThreadPool {
  using Callback = std::function<void(void)>;
  using Queue    = std::list<Callback>;
  using Lock     = std::unique_lock<std::mutex>;

  const size_t size_;
  bool running_;
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
      TSDebug(PLUGIN_TAG, "Destroying thread number %lu (%p)", i, thread);
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
      TSDebug(PLUGIN_TAG, "Creating thread number %lu (%p)", i, pool_[i]);
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
    EVP_MD_CTX_destroy(context);
  }

  EVPContext(void) : context(EVP_MD_CTX_create()) { assert(nullptr != context); }
};

struct EVPKey {
  EVP_PKEY *const key;

  ~EVPKey()
  {
    assert(nullptr != key);
    EVP_PKEY_free(key);
  }

  EVPKey(void) : key(EVP_PKEY_new()) { assert(nullptr != key); }

  bool
  assign(const char *const k) const
  {
    assert(nullptr != k);
    const int rc = EVP_PKEY_assign_RSA(key, k);
    assert(1 == rc);
    return 1 == rc;
  }

  template <typename T>
  bool
  assign(T &t)
  {
    return assign(reinterpret_cast<const char *>(t));
  }
};

bool
verify(const byte *const msg, const size_t mlen, const byte *const sig, const size_t slen, EVP_PKEY *const pkey)
{
  assert(nullptr != msg);
  assert(0 < mlen);
  assert(nullptr != sig);
  assert(0 < slen);
  assert(nullptr != pkey);

  if (nullptr == msg || 0 == mlen || nullptr == sig || 0 == slen || nullptr == pkey) {
    return false;
  }

  EVPContext evp;

  {
    const int rc = EVP_DigestVerifyInit(evp.context, nullptr, EVP_sha256(), nullptr, pkey);
    assert(1 == rc);
    if (1 != rc) {
      return false;
    }
  }

  {
    const int rc = EVP_DigestVerifyUpdate(evp.context, msg, mlen);
    assert(1 == rc);
    if (1 != rc) {
      return false;
    }
  }

  ERR_clear_error();

  {
    const int rc = EVP_DigestVerifyFinal(evp.context, sig, slen);
    return 1 == rc;
  }

  return false;
}

struct Exception {
  ExceptionInfo *info;

  ~Exception()
  {
    assert(nullptr != info);
    info = DestroyExceptionInfo(info);
  }

  Exception(void) : info(AcquireExceptionInfo()) { assert(nullptr != info); }
};

struct Image {
  ImageInfo *info;

  ~Image()
  {
    assert(nullptr != info);
    info = DestroyImageInfo(info);
  }

  Image(void) : info(AcquireImageInfo()) { assert(nullptr != info); }
};

struct Wand {
  MagickWand *wand;
  void *blob;

  ~Wand()
  {
    assert(nullptr != wand);
    wand = DestroyMagickWand(wand);
    if (nullptr == blob) {
      blob = MagickRelinquishMemory(blob);
    }
  }

  Wand(void) : wand(NewMagickWand()), blob(nullptr) { assert(nullptr != wand); }

  void
  clear(void) const
  {
    assert(nullptr != wand);
    ClearMagickWand(wand);
  }

  std::string_view
  get(void)
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

  Core(void) { MagickCoreGenesis("/tmp", MagickFalse); }
};

} // namespace magick

struct QueryMap {
  using Vector = StringViewVector;
  using Map    = std::map<std::string_view, Vector>;

  const static Vector emptyValues;

  std::string content_;
  Map map_;

  QueryMap(std::string &&s) : content_(s) { parse(); }

  template <typename T> const Vector &operator[](T &&k) const
  {
    const auto iterator = map_.find(k);
    if (iterator != map_.end()) {
      return iterator->second;
    }
    return emptyValues;
  }

  void
  parse(void)
  {
    std::string_view key;
    std::size_t i = 0, j = 0;
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
    std::size_t s         = 0;
    const TSReturnCode rc = TSStringPercentDecode(v.data(), v.size(), v.data(), v.size(), &s);
    assert(TS_SUCCESS == rc);
    v.resize(s);
  }

  {
    std::size_t s         = 0;
    const TSReturnCode rc = TSBase64Decode(v.data(), v.size(), reinterpret_cast<unsigned char *>(v.data()), v.size(), &s);
    assert(TS_SUCCESS == rc);
    v.resize(s);
  }

  return true;
}

CharPointerVector
QueryParameterToArguments(CharVector &v)
{
  CharPointerVector result;
  result.reserve(32);

  std::size_t i = 0, j = 0;
  bool quote = false;

  for (; i < v.size(); ++i) {
    char &c = v[i];
    assert('\0' != c);
    if ('"' == c) {
      if (i > j) {
        result.push_back(&v[j]);
      }
      c     = '\0';
      j     = i + 1;
      quote = !quote;
    } else if (!quote && ' ' == c) {
      if (i > j) {
        result.push_back(&v[j]);
      }
      c = '\0';
      j = i + 1;
    }
  }
  if (i > j) {
    result.push_back(&v[j]);
  }
  return result;
}

struct ImageTransform : TransformationPlugin {
  ~ImageTransform() override {}

  ImageTransform(Transaction &t, CharVector &&a, CharPointerVector &&m, ThreadPool &p)
    : TransformationPlugin(t, TransformationPlugin::RESPONSE_TRANSFORMATION),
      arguments_(std::move(a)),
      argumentMap_(std::move(m)),
      threadPool_(p)
  {
    TSDebug(PLUGIN_TAG, "ImageTransform");
  }

  void
  consume(const std::string_view s) override
  {
    TSDebug(PLUGIN_TAG, "consume");
    blob_.insert(blob_.end(), s.begin(), s.end());
  }

  void
  handleInputComplete(void) override
  {
    TSDebug(PLUGIN_TAG, "handleInputComplete");

    threadPool_.emplace_back([this](void) {
      magick::Image image;
      magick::Exception exception;
      magick::Wand wand;

      assert(!this->blob_.empty());

      wand.readBlob(this->blob_);
      wand.write("mpr:b");

      const bool result = MagickCommandGenesis(image.info, ConvertImageCommand, this->argumentMap_.size(),
                                               this->argumentMap_.data(), nullptr, exception.info) == MagickTrue;

      wand.clear();
      wand.read("mpr:a");

      const std::string_view output = wand.get();
      this->produce(output);

      TSDebug(PLUGIN_TAG, "Background transformation is done, resuming continuation (%p)", this);

      this->setOutputComplete();
    });

    TSDebug(PLUGIN_TAG, "Scheduling background transformation (%p)", this);
  }

  CharVector arguments_;
  CharPointerVector argumentMap_;
  CharVector blob_;
  ThreadPool &threadPool_;
};

struct GlobalHookPlugin : GlobalPlugin {
  magick::Core core_;
  magick::EVPKey *key_;
  ThreadPool threadPool_;

  ~GlobalHookPlugin()
  {
    if (nullptr != key_) {
      delete key_;
      key_ = nullptr;
    }
  }

  GlobalHookPlugin(const char *const f = nullptr) : key_(nullptr), threadPool_(2)
  {
    if (nullptr != f) {
      assert(0 < strlen(f));
      TSDebug(PLUGIN_TAG, "public key file: %s", f);
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
      TSDebug(PLUGIN_TAG, "Content-Type is compatible: %s", contentType.c_str());
      const QueryMap queryMap(t.getServerRequest().getUrl().getQuery());
      const auto &magickQueryParameter = queryMap["magick"];
      if (!magickQueryParameter.empty()) {
        const auto &view = magickQueryParameter.front();
        CharVector magick(view.data(), view.data() + view.size());

        bool verified = nullptr == key_;

        if (!verified) {
          const auto &magickSigQueryParameter = queryMap["magickSig"];
          if (!magickSigQueryParameter.empty()) {
            const auto &view2 = magickSigQueryParameter.front();
            CharVector magickSig(view2.data(), view2.data() + view2.size());
            magickSig.insert(magickSig.end(), '\0');
            TSDebug(PLUGIN_TAG, "Magick Signature: %s", magickSig.data());
            QueryParameterToCharVector(magickSig);
            verified = magick::verify(reinterpret_cast<const byte *>(magick.data()), magick.size(),
                                      reinterpret_cast<const byte *>(magickSig.data()), magickSig.size(), key_->key);
          }
        }

        if (verified) {
          magick.insert(magick.end(), '\0');
          QueryParameterToCharVector(magick);
          TSDebug(PLUGIN_TAG, "ImageMagick's syntax: %s", magick.data());
          CharPointerVector argumentMap = QueryParameterToArguments(magick);
          t.addPlugin(new ImageTransform(t, std::move(magick), std::move(argumentMap), threadPool_));
        } else {
          TSDebug(PLUGIN_TAG, "signature verification failed.");
          TSError("[" PLUGIN_TAG "] signature verification failed.");
          t.setStatusCode(HTTP_STATUS_FORBIDDEN);
          t.error();
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

  const char *key = nullptr;

  if (1 < argc) {
    // first argument is the path to the public key used to verify query parameter magick's content.
    key = argv[1];
  }

  new GlobalHookPlugin(key);
}
