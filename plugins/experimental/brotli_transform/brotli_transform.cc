#include <iostream>
#include <vector>
#include <zlib.h>
#include <atscppapi/GlobalPlugin.h>
#include <atscppapi/TransformationPlugin.h>
#include <atscppapi/PluginInit.h>
#include <atscppapi/Logger.h>
#include <brotli/enc/encode.h>

using namespace atscppapi;
using namespace std;

#define TAG "brotli_transformation"

namespace {
const int WINDOW_BITS = 31; // Always use 31 for gzip.
unsigned int INFLATE_SCALE_FACTOR = 6;
unsigned int BROTLI_QUALITY = 9;
}

class GzipInflateTransformationState : noncopyable {
public:
    z_stream z_stream_;
    bool z_stream_initialized_;
    int64_t bytes_produced_;
    TransformationPlugin::Type transformation_type_;

    GzipInflateTransformationState(TransformationPlugin::Type type) : z_stream_initialized_(false), bytes_produced_(0), transformation_type_(type) {
        memset(&z_stream_, 0, sizeof(z_stream_));

        int err = inflateInit2(&z_stream_, WINDOW_BITS);
        if (Z_OK != err) {
            TS_ERROR(TAG, "inflateInit2 failed with error code '%d'.", err);
        } else {
            z_stream_initialized_ = true;
        }
    };

    ~GzipInflateTransformationState() {
        if (z_stream_initialized_) {
            int err = inflateEnd(&z_stream_);
            if (Z_OK != err && Z_STREAM_END != err) {
                TS_ERROR(TAG, "Unable to inflateEnd(), returned error code '%d'", err);
            }
        }
    };
};

class BrotliVecOut : public brotli::BrotliOut {
public:
    BrotliVecOut(vector<char>& out) : outVec(out) {}
    bool Write(const void* buf, size_t n) {
        outVec.insert(outVec.end(), (char*)buf, (char*)buf+n);
        return true;
    }
private:
    vector<char>& outVec;
};

class BrotliTransformationPlugin : public TransformationPlugin {
public:
    BrotliTransformationPlugin(Transaction &transaction) : TransformationPlugin(transaction, RESPONSE_TRANSFORMATION), transaction_(transaction) {
        registerHook(HOOK_READ_RESPONSE_HEADERS);
        serverReturnedContentEncoding();
    }

    void handleReadResponseHeaders(Transaction &transaction) {
        TS_DEBUG(TAG, "BrotliTransformationPlugin handle Read Response Headers.");
        transaction.getServerResponse().getHeaders().set("Content-Encoding", "br");
        transaction.resume();
    }

    /*
     * Move the brotli streaming encode here after Brotli library supports streaming encode
     * */
    void consume(const string &data) {
        string uncompressedData = data;
        if (osContentEncoding_ == GZIP) {
            uncompressedData = gzipInflateData(data);
        }
        buffer_.append(uncompressedData);
    }

    /*
     * No streaming encode for Brotli now,
     * ungzip the gzip data,
     * then using brotli to do compression
     * */
    void handleInputComplete() {
        if (osContentEncoding_ != OTHER) {
            TS_DEBUG(TAG, "BrotliTransformationPlugin handle Input Complete.");
            brotli::BrotliParams params;
            params.quality = BROTLI_QUALITY;

            const char* dataPtr = buffer_.c_str();
            brotli::BrotliMemIn brotliIn(dataPtr, buffer_.length());
            vector<char> out;
            BrotliVecOut brotliOut(out);
            if (!brotli::BrotliCompress(params, &brotliIn, &brotliOut)) {
                TS_DEBUG(TAG, "brotli compress buffer error~");
                outputData_ = buffer_;
            } else {
                outputData_ = string(out.begin(), out.end());
            }
        } else {
            outputData_ = buffer_;
        }
        produce(outputData_);
        setOutputComplete();
    }

    virtual ~BrotliTransformationPlugin() {}

private:
    void serverReturnedContentEncoding() {
        Headers& hdr = transaction_.getServerResponse().getHeaders();
        string contentEncoding = hdr.values("Content-Encoding");
        if (contentEncoding.empty()) {
            osContentEncoding_ = PLAINTEXT;
        } else {
            if (contentEncoding.find("gzip") != string::npos) {
                osContentEncoding_ = GZIP;
                gzipState_ = new GzipInflateTransformationState(RESPONSE_TRANSFORMATION);
            } else {
                osContentEncoding_ = OTHER;
            }
        }
    }

    string gzipInflateData(const string &data) {
        string inflateStr = "";
        if (data.size() == 0) {
            return "";
        }

        if (!gzipState_->z_stream_initialized_) {
            TS_ERROR(TAG, "Unable to inflate output because the z_stream was not initialized.");
            return "";
        }

        int err = Z_OK;
        int iteration = 0;
        int inflate_block_size = INFLATE_SCALE_FACTOR * data.size();
        vector<char> buffer(inflate_block_size);

        // Setup the compressed input
        gzipState_->z_stream_.next_in  = reinterpret_cast<unsigned char *>(const_cast<char *>(data.c_str()));
        gzipState_->z_stream_.avail_in = data.size();

        // Loop while we have more data to inflate
        while (gzipState_->z_stream_.avail_in > 0 && err != Z_STREAM_END) {
            TS_DEBUG(TAG, "Iteration %d: Gzip has %d bytes to inflate", ++iteration, gzipState_->z_stream_.avail_in);

            // Setup where the decompressed output will go.
            gzipState_->z_stream_.next_out  = reinterpret_cast<unsigned char *>(&buffer[0]);
            gzipState_->z_stream_.avail_out = inflate_block_size;

            /* Uncompress */
            err = inflate(&gzipState_->z_stream_, Z_SYNC_FLUSH);

            if (err != Z_OK && err != Z_STREAM_END) {
                TS_ERROR(TAG, "Iteration %d: Inflate failed with error '%d'", iteration, err);
                gzipState_->z_stream_.next_out = NULL;
                return "";
            }

            TS_DEBUG(TAG, "Iteration %d: Gzip inflated a total of %d bytes, producingOutput...", iteration,
              (inflate_block_size - gzipState_->z_stream_.avail_out));
            inflateStr.append(string(&buffer[0], (inflate_block_size - gzipState_->z_stream_.avail_out)));
            gzipState_->bytes_produced_ += (inflate_block_size - gzipState_->z_stream_.avail_out);
        }
        gzipState_->z_stream_.next_out = NULL;
        return inflateStr;
    }

    Transaction &transaction_;
    string buffer_;
    string outputData_;
    GzipInflateTransformationState *gzipState_;
    enum ContentEncoding { GZIP, PLAINTEXT, OTHER };
    ContentEncoding osContentEncoding_;
};

class GlobalHookPlugin : public GlobalPlugin {
public:
    GlobalHookPlugin() {
        registerHook(HOOK_READ_RESPONSE_HEADERS);
    }

    void handleReadResponseHeaders(Transaction &transaction) {
        if (browserSupportBrotli(transaction)) {
            TS_DEBUG(TAG, "Browser support brotli~");
            transaction.addPlugin(new BrotliTransformationPlugin(transaction));
        } else {
            TS_DEBUG(TAG, "Browser does not support brotli~");
        }
        transaction.resume();
    }

private:
    bool browserSupportBrotli(Transaction &transaction) {
        Headers &clientRequestHeaders = transaction.getClientRequest().getHeaders();
        string acceptEncoding = clientRequestHeaders.values("Accept-Encoding");
        TS_DEBUG(TAG, "accept_encoding:[%s]", acceptEncoding.c_str());
        if (!acceptEncoding.empty() && acceptEncoding.find("br") != string::npos) {
            return true;
        }
        return false;
    }
};

void TSPluginInit(int argc, const char *argv[]) {
    RegisterGlobalPlugin("CPP_Brotli_Transform", "apache", "dev@trafficserver.apache.org");
    TS_DEBUG(TAG, "TSPluginInit");
    new GlobalHookPlugin();
}
