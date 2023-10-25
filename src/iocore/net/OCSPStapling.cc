/** @file

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

#include "P_OCSPStapling.h"

#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include <openssl/asn1.h>
#include <openssl/asn1t.h>

#include "tscore/ink_memory.h"
#include "tscore/Encoding.h"
#include "tscore/ink_base64.h"
#include "tscore/ink_string.h"
#include "P_Net.h"
#include "P_SSLConfig.h"
#include "P_SSLUtils.h"
#include "iocore/net/SSLStats.h"
#include "api/FetchSM.h"

// Macros for ASN1 and the code in TS_OCSP_* functions were borrowed from OpenSSL 3.1.0 (a92271e03a8d0dee507b6f1e7f49512568b2c7ad),
// and were modified to make them compilable with BoringSSL and C++ compiler.

// Maximum OCSP stapling response size.
// This should be the response for a single certificate and will typically include the responder certificate chain,
// so 10K should be more than enough.
#define MAX_STAPLING_DER 10240

// maximum length allowed for the encoded OCSP GET request; per RFC 6960, Appendix A, the encoded request must be less than 255
// bytes
#define MAX_OCSP_GET_ENCODED_LENGTH 255 // maximum of 254 bytes + \0

extern ClassAllocator<FetchSM> FetchSMAllocator;

// clang-format off
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
// RFC 6960
using TS_OCSP_CERTID = struct ocsp_cert_id {
  X509_ALGOR *hashAlgorithm;
  ASN1_OCTET_STRING *issuerNameHash;
  ASN1_OCTET_STRING *issuerKeyHash;
  ASN1_INTEGER *serialNumber;
};
DECLARE_ASN1_FUNCTIONS(TS_OCSP_CERTID)
ASN1_SEQUENCE(TS_OCSP_CERTID) = {
        ASN1_SIMPLE(TS_OCSP_CERTID, hashAlgorithm, X509_ALGOR),
        ASN1_SIMPLE(TS_OCSP_CERTID, issuerNameHash, ASN1_OCTET_STRING),
        ASN1_SIMPLE(TS_OCSP_CERTID, issuerKeyHash, ASN1_OCTET_STRING),
        ASN1_SIMPLE(TS_OCSP_CERTID, serialNumber, ASN1_INTEGER)
} ASN1_SEQUENCE_END(TS_OCSP_CERTID)
IMPLEMENT_ASN1_FUNCTIONS(TS_OCSP_CERTID)
TS_OCSP_CERTID * TS_OCSP_CERTID_dup(const TS_OCSP_CERTID *x) \
{
  return static_cast<TS_OCSP_CERTID *>(ASN1_item_dup(ASN1_ITEM_rptr(TS_OCSP_CERTID), const_cast<TS_OCSP_CERTID *>(x)));
}

using TS_OCSP_ONEREQ = struct ocsp_one_request_st {
  TS_OCSP_CERTID *reqCert;
  STACK_OF(X509_EXTENSION) *singleRequestExtensions;
};
DECLARE_ASN1_FUNCTIONS(TS_OCSP_ONEREQ)
ASN1_SEQUENCE(TS_OCSP_ONEREQ) = {
        ASN1_SIMPLE(TS_OCSP_ONEREQ, reqCert, TS_OCSP_CERTID),
        ASN1_EXP_SEQUENCE_OF_OPT(TS_OCSP_ONEREQ, singleRequestExtensions, X509_EXTENSION, 0)
} ASN1_SEQUENCE_END(TS_OCSP_ONEREQ)
IMPLEMENT_ASN1_FUNCTIONS(TS_OCSP_ONEREQ)

using TS_OCSP_REQINFO = struct ocsp_req_info_st {
  ASN1_INTEGER *version;
  GENERAL_NAME *requestorName;
  STACK_OF(TS_OCSP_ONEREQ) *requestList;
  STACK_OF(X509_EXTENSION) *requestExtensions;
};
DECLARE_ASN1_FUNCTIONS(TS_OCSP_REQINFO)
ASN1_SEQUENCE(TS_OCSP_REQINFO) = {
        ASN1_EXP_OPT(TS_OCSP_REQINFO, version, ASN1_INTEGER, 0),
        ASN1_EXP_OPT(TS_OCSP_REQINFO, requestorName, GENERAL_NAME, 1),
        ASN1_SEQUENCE_OF(TS_OCSP_REQINFO, requestList, TS_OCSP_ONEREQ),
        ASN1_EXP_SEQUENCE_OF_OPT(TS_OCSP_REQINFO, requestExtensions, X509_EXTENSION, 2)
} ASN1_SEQUENCE_END(TS_OCSP_REQINFO)
IMPLEMENT_ASN1_FUNCTIONS(TS_OCSP_REQINFO)

using TS_OCSP_SIGNATURE = struct ocsp_signature_st {
    X509_ALGOR *signatureAlgorithm;
    ASN1_BIT_STRING *signature;
    STACK_OF(X509) *certs;
};
ASN1_SEQUENCE(TS_OCSP_SIGNATURE) = {
        ASN1_SIMPLE(TS_OCSP_SIGNATURE, signatureAlgorithm, X509_ALGOR),
        ASN1_SIMPLE(TS_OCSP_SIGNATURE, signature, ASN1_BIT_STRING),
        ASN1_EXP_SEQUENCE_OF_OPT(TS_OCSP_SIGNATURE, certs, X509, 0)
} ASN1_SEQUENCE_END(TS_OCSP_SIGNATURE)
IMPLEMENT_ASN1_FUNCTIONS(TS_OCSP_SIGNATURE)

using TS_OCSP_REQUEST = struct ocsp_req {
 TS_OCSP_REQINFO *tbsRequest;
 TS_OCSP_SIGNATURE *optionalSignature;
};
DECLARE_ASN1_FUNCTIONS(TS_OCSP_REQUEST)
ASN1_SEQUENCE(TS_OCSP_REQUEST) = {
        ASN1_SIMPLE(TS_OCSP_REQUEST, tbsRequest, TS_OCSP_REQINFO),
        ASN1_EXP_OPT(TS_OCSP_REQUEST, optionalSignature, TS_OCSP_SIGNATURE, 0)
} ASN1_SEQUENCE_END(TS_OCSP_REQUEST)
IMPLEMENT_ASN1_FUNCTIONS(TS_OCSP_REQUEST)

using TS_OCSP_RESPBYTES = struct ocsp_resp_bytes_st {
  ASN1_OBJECT *responseType;
  ASN1_OCTET_STRING *response;
};
DECLARE_ASN1_FUNCTIONS(TS_OCSP_RESPBYTES)
ASN1_SEQUENCE(TS_OCSP_RESPBYTES) = {
            ASN1_SIMPLE(TS_OCSP_RESPBYTES, responseType, ASN1_OBJECT),
            ASN1_SIMPLE(TS_OCSP_RESPBYTES, response, ASN1_OCTET_STRING)
} ASN1_SEQUENCE_END(TS_OCSP_RESPBYTES)
IMPLEMENT_ASN1_FUNCTIONS(TS_OCSP_RESPBYTES)

using TS_OCSP_RESPONSE = struct ocsp_resp {
  ASN1_ENUMERATED *responseStatus;
  TS_OCSP_RESPBYTES  *responseBytes;
};
DECLARE_ASN1_FUNCTIONS(TS_OCSP_RESPONSE)
ASN1_SEQUENCE(TS_OCSP_RESPONSE) = {
        ASN1_SIMPLE(TS_OCSP_RESPONSE, responseStatus, ASN1_ENUMERATED),
        ASN1_EXP_OPT(TS_OCSP_RESPONSE, responseBytes, TS_OCSP_RESPBYTES, 0)
} ASN1_SEQUENCE_END(TS_OCSP_RESPONSE)
IMPLEMENT_ASN1_FUNCTIONS(TS_OCSP_RESPONSE)

using TS_OCSP_RESPID = struct ocsp_responder_id_st {
  int type;
  union {
    X509_NAME *byName;
    ASN1_OCTET_STRING *byKey;
  } value;
};
DECLARE_ASN1_FUNCTIONS(TS_OCSP_RESPID)
ASN1_CHOICE(TS_OCSP_RESPID) = {
           ASN1_EXP(TS_OCSP_RESPID, value.byName, X509_NAME, 1),
           ASN1_EXP(TS_OCSP_RESPID, value.byKey, ASN1_OCTET_STRING, 2)
} ASN1_CHOICE_END(TS_OCSP_RESPID)
IMPLEMENT_ASN1_FUNCTIONS(TS_OCSP_RESPID)

using TS_OCSP_REVOKEDINFO = struct ocsp_revoked_info_st {
    ASN1_GENERALIZEDTIME *revocationTime;
    ASN1_ENUMERATED *revocationReason;
};
DECLARE_ASN1_FUNCTIONS(TS_OCSP_REVOKEDINFO)
ASN1_SEQUENCE(TS_OCSP_REVOKEDINFO) = {
        ASN1_SIMPLE(TS_OCSP_REVOKEDINFO, revocationTime, ASN1_GENERALIZEDTIME),
        ASN1_EXP_OPT(TS_OCSP_REVOKEDINFO, revocationReason, ASN1_ENUMERATED, 0)
} ASN1_SEQUENCE_END(TS_OCSP_REVOKEDINFO)
IMPLEMENT_ASN1_FUNCTIONS(TS_OCSP_REVOKEDINFO)

using TS_OCSP_CERTSTATUS = struct ocsp_cert_status_st {
  int type;
  union {
      ASN1_NULL *good;
      TS_OCSP_REVOKEDINFO *revoked;
      ASN1_NULL *unknown;
  } value;
};
DECLARE_ASN1_FUNCTIONS(TS_OCSP_CERTSTATUS)
ASN1_CHOICE(TS_OCSP_CERTSTATUS) = {
        ASN1_IMP(TS_OCSP_CERTSTATUS, value.good, ASN1_NULL, 0),
        ASN1_IMP(TS_OCSP_CERTSTATUS, value.revoked, TS_OCSP_REVOKEDINFO, 1),
        ASN1_IMP(TS_OCSP_CERTSTATUS, value.unknown, ASN1_NULL, 2)
} ASN1_CHOICE_END(TS_OCSP_CERTSTATUS)
IMPLEMENT_ASN1_FUNCTIONS(TS_OCSP_CERTSTATUS)

using TS_OCSP_SINGLERESP = struct ocsp_single_response_st {
  TS_OCSP_CERTID *certId;
  TS_OCSP_CERTSTATUS *certStatus;
  ASN1_GENERALIZEDTIME *thisUpdate;
  ASN1_GENERALIZEDTIME *nextUpdate;
  STACK_OF(X509_EXTENSION) *singleExtensions;
};
DECLARE_ASN1_FUNCTIONS(TS_OCSP_SINGLERESP)
ASN1_SEQUENCE(TS_OCSP_SINGLERESP) = {
           ASN1_SIMPLE(TS_OCSP_SINGLERESP, certId, TS_OCSP_CERTID),
           ASN1_SIMPLE(TS_OCSP_SINGLERESP, certStatus, TS_OCSP_CERTSTATUS),
           ASN1_SIMPLE(TS_OCSP_SINGLERESP, thisUpdate, ASN1_GENERALIZEDTIME),
           ASN1_EXP_OPT(TS_OCSP_SINGLERESP, nextUpdate, ASN1_GENERALIZEDTIME, 0),
           ASN1_EXP_SEQUENCE_OF_OPT(TS_OCSP_SINGLERESP, singleExtensions, X509_EXTENSION, 1)
} ASN1_SEQUENCE_END(TS_OCSP_SINGLERESP)
IMPLEMENT_ASN1_FUNCTIONS(TS_OCSP_SINGLERESP)

using TS_OCSP_RESPDATA = struct ocsp_response_data_st {
  ASN1_INTEGER *version;
  TS_OCSP_RESPID *responderId;
  ASN1_GENERALIZEDTIME *producedAt;
  STACK_OF(TS_OCSP_SINGLERESP) *responses;
  STACK_OF(X509_EXTENSION) *responseExtensions;
};
DECLARE_ASN1_FUNCTIONS(TS_OCSP_RESPDATA)
ASN1_SEQUENCE(TS_OCSP_RESPDATA) = {
           ASN1_EXP_OPT(TS_OCSP_RESPDATA, version, ASN1_INTEGER, 0),
           ASN1_SIMPLE(TS_OCSP_RESPDATA, responderId, TS_OCSP_RESPID),
           ASN1_SIMPLE(TS_OCSP_RESPDATA, producedAt, ASN1_GENERALIZEDTIME),
           ASN1_SEQUENCE_OF(TS_OCSP_RESPDATA, responses, TS_OCSP_SINGLERESP),
           ASN1_EXP_SEQUENCE_OF_OPT(TS_OCSP_RESPDATA, responseExtensions, X509_EXTENSION, 1)
} ASN1_SEQUENCE_END(TS_OCSP_RESPDATA)
IMPLEMENT_ASN1_FUNCTIONS(TS_OCSP_RESPDATA)

using TS_OCSP_BASICRESP = struct ocsp_basic_response_st {
  TS_OCSP_RESPDATA *tbsResponseData;
  X509_ALGOR *signatureAlgorithm;
  ASN1_BIT_STRING *signature;
  STACK_OF(X509) *certs;
};
DECLARE_ASN1_FUNCTIONS(TS_OCSP_BASICRESP)
ASN1_SEQUENCE(TS_OCSP_BASICRESP) = {
           ASN1_SIMPLE(TS_OCSP_BASICRESP, tbsResponseData, TS_OCSP_RESPDATA),
           ASN1_SIMPLE(TS_OCSP_BASICRESP, signatureAlgorithm, X509_ALGOR),
           ASN1_SIMPLE(TS_OCSP_BASICRESP, signature, ASN1_BIT_STRING),
           ASN1_EXP_SEQUENCE_OF_OPT(TS_OCSP_BASICRESP, certs, X509, 0)
} ASN1_SEQUENCE_END(TS_OCSP_BASICRESP)
IMPLEMENT_ASN1_FUNCTIONS(TS_OCSP_BASICRESP)

DEFINE_STACK_OF(TS_OCSP_ONEREQ)
DEFINE_STACK_OF(TS_OCSP_SINGLERESP)

; // This is to satisfy clang-format
// clang-format on

namespace
{

constexpr int TS_OCSP_RESPONSE_STATUS_SUCCESSFUL = 0;
// constexpr int TS_OCSP_RESPONSE_STATUS_MALFORMEDREQUEST  = 1;
// constexpr int TS_OCSP_RESPONSE_STATUS_INTERNALERROR     = 2;
// constexpr int TS_OCSP_RESPONSE_STATUS_TRYLATER          = 3;
// 4 is not defined on the spec
// constexpr int TS_OCSP_RESPONSE_STATUS_SIGREQUIRED       = 5;
// constexpr int TS_OCSP_RESPONSE_STATUS_UNAUTHORIZED      = 6;
constexpr int TS_OCSP_CERTSTATUS_GOOD    = 0;
constexpr int TS_OCSP_CERTSTATUS_REVOKED = 1;
constexpr int TS_OCSP_CERTSTATUS_UNKNOWN = 2;
#pragma GCC diagnostic pop

// End of definitions from RFC 6960

// Cached info stored in SSL_CTX ex_info
struct certinfo {
  unsigned char idx[20]; // Index in session cache SHA1 hash of certificate
  TS_OCSP_CERTID *cid;   // Certificate ID for OCSP requests or nullptr if ID cannot be determined
  char *uri;             // Responder details
  char *certname;
  char *user_agent;
  ink_mutex stapling_mutex;
  unsigned char resp_der[MAX_STAPLING_DER];
  unsigned int resp_derlen;
  bool is_prefetched;
  bool is_expire;
  time_t expire_time;
};

class HTTPRequest : public Continuation
{
public:
  static constexpr int MAX_RESP_LEN = 100 * 1024;

  HTTPRequest()
  {
    mutex = new_ProxyMutex();
    SET_HANDLER(&HTTPRequest::event_handler);
  }

  ~HTTPRequest()
  {
    this->_fsm->ext_destroy();
    OPENSSL_free(this->_req_body);
  }

  int
  event_handler(int event, Event *e)
  {
    if (event == TS_EVENT_IMMEDIATE) {
      this->fetch();
    } else {
      if (event == TS_FETCH_EVENT_EXT_BODY_DONE) {
        this->set_done();
      } else if (event == TS_EVENT_ERROR) {
        this->set_error();
      }
    }

    return 0;
  }

  void
  set_request_line(bool use_get, const char *uri)
  {
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family      = AF_INET;
    sin.sin_addr.s_addr = inet_addr("127.0.0.1");
    sin.sin_port        = 65535;

    this->_fsm = FetchSMAllocator.alloc();
    this->_fsm->ext_init(this, use_get ? "GET" : "POST", uri, "HTTP/1.1", reinterpret_cast<sockaddr *>(&sin),
                         TS_FETCH_FLAGS_SKIP_REMAP);
  }

  int
  set_body(const char *content_type, const ASN1_ITEM *it, const ASN1_VALUE *req)
  {
    this->_req_body = nullptr;

    if (req != nullptr) {
      // const_cast is needed for OpenSSL 1.1.1
      this->_req_body_len = ASN1_item_i2d(const_cast<ASN1_VALUE *>(req), &this->_req_body, it);
      if (this->_req_body_len == -1) {
        return 0;
      }
    }
    this->add_header("Content-Type", content_type);
    char req_body_len_str[10];
    int req_body_len_str_len;
    req_body_len_str_len = ink_fast_itoa(this->_req_body_len, req_body_len_str, sizeof(req_body_len_str));
    this->add_header("Content-Length", 14, req_body_len_str, req_body_len_str_len);

    return 1;
  }

  void
  add_header(const char *name, int name_len, const char *value, int value_len)
  {
    this->_fsm->ext_add_header(name, name_len, value, value_len);
  }

  void
  add_header(const char *name, const char *value)
  {
    this->add_header(name, strlen(name), value, strlen(value));
  }

  bool
  is_initiated()
  {
    SCOPED_MUTEX_LOCK(lock, mutex, this_ethread());
    return this->_result != INT_MAX;
  }

  bool
  is_done()
  {
    SCOPED_MUTEX_LOCK(lock, mutex, this_ethread());
    return this->_result != 0 && this->_result != INT_MAX;
  }

  bool
  is_success()
  {
    SCOPED_MUTEX_LOCK(lock, mutex, this_ethread());
    return this->_result == 1;
  }

  unsigned char *
  get_response_body(int *len)
  {
    SCOPED_MUTEX_LOCK(lock, mutex, this_ethread());
    char *buf = static_cast<char *>(ats_malloc(MAX_RESP_LEN));
    *len      = this->_fsm->ext_read_data(buf, MAX_RESP_LEN);
    return reinterpret_cast<unsigned char *>(buf);
  }

private:
  FetchSM *_fsm            = nullptr;
  unsigned char *_req_body = nullptr;
  int _req_body_len        = 0;
  int _result              = INT_MAX;

  void
  fetch()
  {
    SCOPED_MUTEX_LOCK(lock, mutex, this_ethread());
    this->_result = 0;
    this->_fsm->ext_launch();
    this->_fsm->ext_write_data(this->_req_body, this->_req_body_len);
  }

  void
  set_done()
  {
    SCOPED_MUTEX_LOCK(lock, mutex, this_ethread());
    this->_result = 1;
  }

  void
  set_error()
  {
    SCOPED_MUTEX_LOCK(lock, mutex, this_ethread());
    this->_result = -1;
  }
};

TS_OCSP_CERTID *
TS_OCSP_cert_id_new(const EVP_MD *dgst, const X509_NAME *issuerName, const ASN1_BIT_STRING *issuerKey,
                    const ASN1_INTEGER *serialNumber)
{
  int nid;
  unsigned int i;
  X509_ALGOR *alg;
  TS_OCSP_CERTID *cid = nullptr;
  unsigned char md[EVP_MAX_MD_SIZE];

  if ((cid = TS_OCSP_CERTID_new()) == nullptr)
    goto err;

  alg = cid->hashAlgorithm;
  ASN1_OBJECT_free(alg->algorithm);
  if ((nid = EVP_MD_type(dgst)) == NID_undef) {
    Debug("ssl_ocsp", "Unknown NID");
    goto err;
  }
  if ((alg->algorithm = OBJ_nid2obj(nid)) == nullptr)
    goto err;
  if ((alg->parameter = ASN1_TYPE_new()) == nullptr)
    goto err;
  alg->parameter->type = V_ASN1_NULL;

  if (!X509_NAME_digest(issuerName, dgst, md, &i))
    goto digerr;
  if (!(ASN1_OCTET_STRING_set(cid->issuerNameHash, md, i)))
    goto err;

  /* Calculate the issuerKey hash, excluding tag and length */
  if (!EVP_Digest(issuerKey->data, issuerKey->length, md, &i, dgst, nullptr))
    goto err;

  if (!(ASN1_OCTET_STRING_set(cid->issuerKeyHash, md, i)))
    goto err;

  if (serialNumber) {
    if (ASN1_STRING_copy(cid->serialNumber, serialNumber) == 0)
      goto err;
  }
  return cid;
digerr:
  Debug("ssl_ocsp", "Digest error");
err:
  TS_OCSP_CERTID_free(cid);
  return nullptr;
}

TS_OCSP_CERTID *
TS_OCSP_cert_to_id(const EVP_MD *dgst, const X509 *subject, const X509 *issuer)
{
  const X509_NAME *iname;
  const ASN1_INTEGER *serial;
  ASN1_BIT_STRING *ikey;

  if (!dgst)
    dgst = EVP_sha1();
  if (subject) {
    iname  = X509_get_issuer_name(subject);
    serial = X509_get0_serialNumber(subject);
  } else {
    iname  = X509_get_subject_name(issuer);
    serial = nullptr;
  }
  ikey = X509_get0_pubkey_bitstr(issuer);
  return TS_OCSP_cert_id_new(dgst, iname, ikey, serial);
}

int
TS_OCSP_check_validity(ASN1_GENERALIZEDTIME *thisupd, ASN1_GENERALIZEDTIME *nextupd, long nsec, long maxsec)
{
  int ret = 1;
  time_t t_now, t_tmp;

  time(&t_now);
  /* Check thisUpdate is valid and not more than nsec in the future */
  if (!ASN1_GENERALIZEDTIME_check(thisupd)) {
    Debug("ssl_ocsp", "Error in thisUpdate field");
    ret = 0;
  } else {
    t_tmp = t_now + nsec;
    if (X509_cmp_time(thisupd, &t_tmp) > 0) {
      Debug("ssl_ocsp", "Status not yet valid");
      ret = 0;
    }

    /*
     * If maxsec specified check thisUpdate is not more than maxsec in
     * the past
     */
    if (maxsec >= 0) {
      t_tmp = t_now - maxsec;
      if (X509_cmp_time(thisupd, &t_tmp) < 0) {
        Debug("ssl_ocsp", "Status too old");
        ret = 0;
      }
    }
  }

  if (nextupd == nullptr)
    return ret;

  /* Check nextUpdate is valid and not more than nsec in the past */
  if (!ASN1_GENERALIZEDTIME_check(nextupd)) {
    Debug("ssl_ocsp", "Error in nextUpdate field");
    ret = 0;
  } else {
    t_tmp = t_now - nsec;
    if (X509_cmp_time(nextupd, &t_tmp) < 0) {
      Debug("ssl_ocsp", "Status expired");
      ret = 0;
    }
  }

  /* Also don't allow nextUpdate to precede thisUpdate */
  if (ASN1_STRING_cmp(nextupd, thisupd) < 0) {
    Debug("ssl_ocsp", "nextUpdate precedes thisUpdate");
    ret = 0;
  }

  return ret;
}

TS_OCSP_ONEREQ *
TS_OCSP_request_add0_id(TS_OCSP_REQUEST *req, TS_OCSP_CERTID *cid)
{
  TS_OCSP_ONEREQ *one = nullptr;

  if ((one = TS_OCSP_ONEREQ_new()) == nullptr)
    return nullptr;
  TS_OCSP_CERTID_free(one->reqCert);
  one->reqCert = cid;
  if (req && !sk_TS_OCSP_ONEREQ_push(req->tbsRequest->requestList, one)) {
    one->reqCert = nullptr; /* do not free on error */
    TS_OCSP_ONEREQ_free(one);
    return nullptr;
  }
  return one;
}

TS_OCSP_BASICRESP *
TS_OCSP_response_get1_basic(TS_OCSP_RESPONSE *resp)
{
  TS_OCSP_RESPBYTES *rb = resp->responseBytes;

  if (rb == nullptr) {
    Debug("ssl_ocsp", "No response data");
    return nullptr;
  }
  if (OBJ_obj2nid(rb->responseType) != NID_id_pkix_OCSP_basic) {
    Debug("ssl_ocsp", "Not basic response");
    return nullptr;
  }

  return static_cast<TS_OCSP_BASICRESP *>(ASN1_item_unpack(rb->response, ASN1_ITEM_rptr(TS_OCSP_BASICRESP)));
}

int
TS_OCSP_id_issuer_cmp(const TS_OCSP_CERTID *a, const TS_OCSP_CERTID *b)
{
  int ret;
  ret = OBJ_cmp(a->hashAlgorithm->algorithm, b->hashAlgorithm->algorithm);
  if (ret)
    return ret;
  ret = ASN1_OCTET_STRING_cmp(a->issuerNameHash, b->issuerNameHash);
  if (ret)
    return ret;
  return ASN1_OCTET_STRING_cmp(a->issuerKeyHash, b->issuerKeyHash);
}

int
TS_OCSP_id_cmp(const TS_OCSP_CERTID *a, const TS_OCSP_CERTID *b)
{
  int ret;
  ret = TS_OCSP_id_issuer_cmp(a, b);
  if (ret)
    return ret;
  return ASN1_INTEGER_cmp(a->serialNumber, b->serialNumber);
}

int
TS_OCSP_resp_find(TS_OCSP_BASICRESP *bs, TS_OCSP_CERTID *id, int last)
{
  int i;
  STACK_OF(TS_OCSP_SINGLERESP) * sresp;
  TS_OCSP_SINGLERESP *single;

  if (bs == nullptr)
    return -1;
  if (last < 0)
    last = 0;
  else
    last++;
  sresp = bs->tbsResponseData->responses;
  for (i = last; i < static_cast<int>(sk_TS_OCSP_SINGLERESP_num(sresp)); i++) {
    single = sk_TS_OCSP_SINGLERESP_value(sresp, i);
    if (!TS_OCSP_id_cmp(id, single->certId))
      return i;
  }
  return -1;
}

TS_OCSP_SINGLERESP *
TS_OCSP_resp_get0(TS_OCSP_BASICRESP *bs, int idx)
{
  if (bs == nullptr)
    return nullptr;
  return sk_TS_OCSP_SINGLERESP_value(bs->tbsResponseData->responses, idx);
}

int
TS_OCSP_single_get0_status(TS_OCSP_SINGLERESP *single, int *reason, ASN1_GENERALIZEDTIME **revtime, ASN1_GENERALIZEDTIME **thisupd,
                           ASN1_GENERALIZEDTIME **nextupd)
{
  int ret;
  TS_OCSP_CERTSTATUS *cst;

  if (single == nullptr)
    return -1;
  cst = single->certStatus;
  ret = cst->type;
  if (ret == TS_OCSP_CERTSTATUS_REVOKED) {
    TS_OCSP_REVOKEDINFO *rev = cst->value.revoked;

    if (revtime)
      *revtime = rev->revocationTime;
    if (reason) {
      if (rev->revocationReason)
        *reason = ASN1_ENUMERATED_get(rev->revocationReason);
      else
        *reason = -1;
    }
  }
  if (thisupd != nullptr)
    *thisupd = single->thisUpdate;
  if (nextupd != nullptr)
    *nextupd = single->nextUpdate;
  return ret;
}

int
TS_OCSP_resp_find_status(TS_OCSP_BASICRESP *bs, TS_OCSP_CERTID *id, int *status, int *reason, ASN1_GENERALIZEDTIME **revtime,
                         ASN1_GENERALIZEDTIME **thisupd, ASN1_GENERALIZEDTIME **nextupd)
{
  int i = TS_OCSP_resp_find(bs, id, -1);
  TS_OCSP_SINGLERESP *single;

  /* Maybe check for multiple responses and give an error? */
  if (i < 0)
    return 0;
  single = TS_OCSP_resp_get0(bs, i);
  i      = TS_OCSP_single_get0_status(single, reason, revtime, thisupd, nextupd);
  if (i < 0) {
    return 0;
  }
  if (status != nullptr)
    *status = i;
  return 1;
}

} // End of namespace

/*
 * In the case of multiple certificates associated with a SSL_CTX, we must store a map
 * of cached responses
 */
using certinfo_map = std::map<X509 *, certinfo *>;

void
certinfo_map_free(void * /*parent*/, void *ptr, CRYPTO_EX_DATA * /*ad*/, int /*idx*/, long /*argl*/, void * /*argp*/)
{
  certinfo_map *map = static_cast<certinfo_map *>(ptr);

  if (!map) {
    return;
  }

  for (certinfo_map::iterator iter = map->begin(); iter != map->end(); ++iter) {
    certinfo *cinf = iter->second;
    if (cinf->uri) {
      OPENSSL_free(cinf->uri);
    }

    ats_free(cinf->certname);
    ats_free(cinf->user_agent);

    ink_mutex_destroy(&cinf->stapling_mutex);
    OPENSSL_free(cinf);
  }
  delete map;
}

static int ssl_stapling_index = -1;

void
ssl_stapling_ex_init()
{
  if (ssl_stapling_index != -1) {
    return;
  }
  ssl_stapling_index = SSL_CTX_get_ex_new_index(0, nullptr, nullptr, nullptr, certinfo_map_free);
}

static X509 *
stapling_get_issuer(SSL_CTX *ssl_ctx, X509 *x)
{
  X509 *issuer                = nullptr;
  X509_STORE *st              = SSL_CTX_get_cert_store(ssl_ctx);
  STACK_OF(X509) *extra_certs = nullptr;
  X509_STORE_CTX *inctx       = X509_STORE_CTX_new();

  if (inctx == nullptr) {
    return nullptr;
  }

#ifdef SSL_CTX_select_current_cert
  if (!SSL_CTX_select_current_cert(ssl_ctx, x)) {
    Warning("OCSP: could not select current certificate chain %p", x);
  }
#endif

  if (X509_STORE_CTX_init(inctx, st, nullptr, nullptr) == 0) {
    goto end;
  }

#ifdef SSL_CTX_get_extra_chain_certs
  SSL_CTX_get_extra_chain_certs(ssl_ctx, &extra_certs);
#else
  extra_certs = ssl_ctx->extra_certs;
#endif

  if (sk_X509_num(extra_certs) == 0) {
    goto end;
  }

  for (int i = 0; i < static_cast<int>(sk_X509_num(extra_certs)); i++) {
    issuer = sk_X509_value(extra_certs, i);
    if (X509_check_issued(issuer, x) == X509_V_OK) {
#if OPENSSL_VERSION_NUMBER < 0x10100000
      CRYPTO_add(&issuer->references, 1, CRYPTO_LOCK_X509);
      return issuer;
#else
      X509_up_ref(issuer);
      goto end;
#endif
    }
  }

  if (X509_STORE_CTX_get1_issuer(&issuer, inctx, x) <= 0) {
    issuer = nullptr;
  }

end:
  X509_STORE_CTX_free(inctx);

  return issuer;
}

static bool
stapling_cache_response(TS_OCSP_RESPONSE *rsp, certinfo *cinf)
{
  unsigned char resp_der[MAX_STAPLING_DER];
  unsigned char *p;
  unsigned int resp_derlen;

  p           = resp_der;
  resp_derlen = i2d_TS_OCSP_RESPONSE(rsp, &p);

  if (resp_derlen == 0) {
    Error("stapling_cache_response: cannot decode OCSP response for %s", cinf->certname);
    return false;
  }

  if (resp_derlen > MAX_STAPLING_DER) {
    Error("stapling_cache_response: OCSP response too big (%u bytes) for %s", resp_derlen, cinf->certname);
    return false;
  }

  ink_mutex_acquire(&cinf->stapling_mutex);
  memcpy(cinf->resp_der, resp_der, resp_derlen);
  cinf->resp_derlen = resp_derlen;
  cinf->is_expire   = false;
  cinf->expire_time = time(nullptr) + SSLConfigParams::ssl_ocsp_cache_timeout;
  ink_mutex_release(&cinf->stapling_mutex);

  Debug("ssl_ocsp", "stapling_cache_response: success to cache response");
  return true;
}

bool
ssl_stapling_init_cert(SSL_CTX *ctx, X509 *cert, const char *certname, const char *rsp_file)
{
  scoped_X509 issuer;
  STACK_OF(OPENSSL_STRING) *aia = nullptr;
  TS_OCSP_RESPONSE *rsp         = nullptr;

  if (!cert) {
    Error("null cert passed in for %s", certname);
    return false;
  }

  certinfo_map *map = static_cast<certinfo_map *>(SSL_CTX_get_ex_data(ctx, ssl_stapling_index));
  if (map && map->find(cert) != map->end()) {
    Note("certificate already initialized for %s", certname);
    return false;
  }

  if (!map) {
    map = new certinfo_map;
  }
  certinfo *cinf = static_cast<certinfo *>(OPENSSL_malloc(sizeof(certinfo)));
  if (!cinf) {
    Error("error allocating memory for %s", certname);
    delete map;
    return false;
  }

  // Initialize certinfo
  cinf->cid      = nullptr;
  cinf->uri      = nullptr;
  cinf->certname = ats_strdup(certname);
  if (SSLConfigParams::ssl_ocsp_user_agent != nullptr) {
    cinf->user_agent = ats_strdup(SSLConfigParams::ssl_ocsp_user_agent);
  }
  cinf->resp_derlen = 0;
  ink_mutex_init(&cinf->stapling_mutex);
  cinf->is_prefetched = rsp_file ? true : false;
  cinf->is_expire     = true;
  cinf->expire_time   = 0;

  if (cinf->is_prefetched) {
    Debug("ssl_ocsp", "using OCSP prefetched response file %s", rsp_file);
    FILE *fp = fopen(rsp_file, "r");
    if (fp) {
      fseek(fp, 0, SEEK_END);
      long rsp_buf_len = ftell(fp);
      if (rsp_buf_len >= 0) {
        rewind(fp);
        unsigned char *rsp_buf = static_cast<unsigned char *>(malloc(rsp_buf_len));
        auto read_len          = fread(rsp_buf, 1, rsp_buf_len, fp);
        if (read_len == static_cast<size_t>(rsp_buf_len)) {
          const unsigned char *p = rsp_buf;
          rsp                    = d2i_TS_OCSP_RESPONSE(nullptr, &p, rsp_buf_len);
        } else {
          Error("stapling_refresh_response: failed to read prefetched response file: %s", rsp_file);
        }
        free(rsp_buf);
      } else {
        Error("stapling_refresh_response: failed to check the size of prefetched response file: %s", rsp_file);
      }
      fclose(fp);
    }

    if (!fp || !rsp) {
      Note("cannot get prefetched response for %s from %s", certname, rsp_file);
      goto err;
    }

    if (!stapling_cache_response(rsp, cinf)) {
      Error("stapling_refresh_response: can not cache response");
      goto err;
    } else {
      Debug("ssl_ocsp", "stapling_refresh_response: successful refresh OCSP response");
      TS_OCSP_RESPONSE_free(rsp);
      rsp = nullptr;
    }
  }

  issuer.reset(stapling_get_issuer(ctx, cert));
  if (issuer.get() == nullptr) {
    Note("cannot get issuer certificate from %s", certname);
    goto err;
  }

  cinf->cid = TS_OCSP_cert_to_id(nullptr, cert, issuer.get());
  if (!cinf->cid) {
    goto err;
  }

  X509_digest(cert, EVP_sha1(), cinf->idx, nullptr);

  aia = X509_get1_ocsp(cert);
  if (aia) {
    cinf->uri = sk_OPENSSL_STRING_pop(aia);
    X509_email_free(aia);
  }

  if (!cinf->uri) {
    Note("no OCSP responder URI for %s", certname);
    goto err;
  }

#ifdef OPENSSL_IS_BORINGSSL
  X509_up_ref(cert);
#endif

  map->insert(std::make_pair(cert, cinf));
  SSL_CTX_set_ex_data(ctx, ssl_stapling_index, map);

  Note("successfully initialized stapling for %s into SSL_CTX: %p uri=%s", certname, ctx, cinf->uri);
  return true;

err:
  if (cinf->cid) {
    TS_OCSP_CERTID_free(cinf->cid);
  }

  ats_free(cinf->certname);
  ats_free(cinf->user_agent);

  if (cinf) {
    OPENSSL_free(cinf);
  }
  if (map) {
    delete map;
  }

  if (rsp) {
    TS_OCSP_RESPONSE_free(rsp);
  }

  return false;
}

static certinfo_map *
stapling_get_cert_info(SSL_CTX *ctx)
{
  certinfo_map *map;

  // Only return the map if it contains at least one element with a valid entry
  map = static_cast<certinfo_map *>(SSL_CTX_get_ex_data(ctx, ssl_stapling_index));
  if (map && !map->empty() && map->begin()->second && map->begin()->second->cid) {
    return map;
  }

  return nullptr;
}

static int
stapling_check_response(certinfo *cinf, TS_OCSP_RESPONSE *rsp)
{
  int status            = TS_OCSP_CERTSTATUS_UNKNOWN, reason;
  TS_OCSP_BASICRESP *bs = nullptr;
  ASN1_GENERALIZEDTIME *rev, *thisupd, *nextupd;
  int response_status = ASN1_ENUMERATED_get(rsp->responseStatus);

  // Check to see if response is an error.
  // If so we automatically accept it because it would have expired from the cache if it was time to retry.
  if (response_status != TS_OCSP_RESPONSE_STATUS_SUCCESSFUL) {
    return SSL_TLSEXT_ERR_NOACK;
  }

  bs = TS_OCSP_response_get1_basic(rsp);
  if (bs == nullptr) {
    // If we can't parse response just pass it back to client
    Error("stapling_check_response: cannot parse response for %s", cinf->certname);
    return SSL_TLSEXT_ERR_OK;
  }
  if (!TS_OCSP_resp_find_status(bs, cinf->cid, &status, &reason, &rev, &thisupd, &nextupd)) {
    // If ID not present just pass it back to client
    Error("stapling_check_response: certificate ID not present in response for %s", cinf->certname);
  } else {
    if (!TS_OCSP_check_validity(thisupd, nextupd, 300, -1)) {
      // The check is just for logging and pass the response back to client anyway
      Error("stapling_check_response: status in response for %s is not valid already/yet", cinf->certname);
    }
  }

  switch (status) {
  case TS_OCSP_CERTSTATUS_GOOD:
    break;
  case TS_OCSP_CERTSTATUS_REVOKED:
    Metrics::increment(ssl_rsb.ocsp_revoked_cert);
    break;
  case TS_OCSP_CERTSTATUS_UNKNOWN:
    Metrics::increment(ssl_rsb.ocsp_unknown_cert);
    break;
  default:
    break;
  }

  TS_OCSP_BASICRESP_free(bs);

  return SSL_TLSEXT_ERR_OK;
}

static TS_OCSP_RESPONSE *
query_responder(const char *uri, const char *user_agent, TS_OCSP_REQUEST *req, int req_timeout, bool use_get)
{
  ink_hrtime start, end;
  TS_OCSP_RESPONSE *resp = nullptr;

  start = ink_get_hrtime();
  end   = ink_hrtime_add(start, ink_hrtime_from_sec(req_timeout));

  HTTPRequest httpreq;

  httpreq.set_request_line(use_get, uri);

  // Host header
  const char *host  = strstr(uri, "://") + 3;
  const char *slash = strchr(host, '/');
  if (slash == nullptr) {
    slash = host + strlen(host);
  }
  int host_len = slash - host;
  httpreq.add_header("Host", 4, host, host_len);

  // User-Agent header
  if (user_agent != nullptr) {
    httpreq.add_header("User-Agent", user_agent);
  }

  // Content-Type, Content-Length, Request Body
  if (!use_get) {
    if (httpreq.set_body("application/ocsp-request", ASN1_ITEM_rptr(TS_OCSP_REQUEST), (const ASN1_VALUE *)req) != 1) {
      Error("failed to make a request for OCSP server; uri=%s", uri);
      return nullptr;
    }
  }

  // Send request
  Event *e = eventProcessor.schedule_imm(&httpreq, ET_NET);

  // Wait until the request completes
  do {
    ink_hrtime_sleep(HRTIME_MSECONDS(1));
  } while (!httpreq.is_done() && (ink_get_hrtime() < end));

  if (!httpreq.is_done()) {
    Error("OCSP request was timed out; uri=%s", uri);
    if (!httpreq.is_initiated()) {
      Debug("ssl_ocsp", "Request is not initiated yet. Cancelling the event.");
      e->cancel(&httpreq);
    }
  }

  if (httpreq.is_success()) {
    // Parse the response
    int len;
    unsigned char *res     = httpreq.get_response_body(&len);
    const unsigned char *p = res;
    resp = reinterpret_cast<TS_OCSP_RESPONSE *>(ASN1_item_d2i(nullptr, &p, len, ASN1_ITEM_rptr(TS_OCSP_RESPONSE)));

    if (resp) {
      ats_free(res);
      return resp;
    }

    if (len < 5) {
      Error("failed to parse a response from OCSP server; uri=%s len=%d", uri, len);
    } else {
      Error("failed to parse a response from OCSP server; uri=%s len=%d data=%02x%02x%02x%02x%02x...", uri, len, res[0], res[1],
            res[2], res[3], res[4]);
    }
    ats_free(res);
    return nullptr;
  }

  Error("failed to get a response from OCSP server; uri=%s", uri);
  return nullptr;
}

// The default encoding table, per the RFC, does not encode any of the following: ,+/=
static const unsigned char encoding_map[32] = {
  0xFF, 0xFF, 0xFF,
  0xFF,       // control
  0xB4,       // space " # %
  0x19,       // , + /
  0x00,       //
  0x0E,       // < > =
  0x00, 0x00, //
  0x00,       //
  0x1E, 0x80, // [ \ ] ^ `
  0x00, 0x00, //
  0x1F,       // { | } ~ DEL
  0x00, 0x00, 0x00,
  0x00, // all non-ascii characters unmodified
  0x00, 0x00, 0x00,
  0x00, //               .
  0x00, 0x00, 0x00,
  0x00, //               .
  0x00, 0x00, 0x00,
  0x00 //               .
};

static IOBufferBlock *
make_url_for_get(TS_OCSP_REQUEST *req, const char *base_url)
{
  unsigned char *ocsp_der = nullptr;
  int ocsp_der_len        = -1;
  char ocsp_encoded_der[MAX_OCSP_GET_ENCODED_LENGTH]; // Stores base64 encoded data
  size_t ocsp_encoded_der_len = 0;
  char ocsp_escaped[MAX_OCSP_GET_ENCODED_LENGTH];
  int ocsp_escaped_len = -1;
  IOBufferBlock *url   = nullptr;

  ocsp_der_len = i2d_TS_OCSP_REQUEST(req, &ocsp_der);

  // ats_base64_encode does not insert newlines, which would need to be removed otherwise.
  // When ats_base64_encode is false, the encoded length exceeds the length of our buffer,
  // which is set to MAX_OCSP_GET_ENCODED_LENGTH; 255 bytes per RFC6960, Appendix A.
  if (ocsp_der_len <= 0 || ocsp_der == nullptr) {
    Error("stapling_refresh_response: unable to convert OCSP request to DER; falling back to POST; url=%s", base_url);
    return nullptr;
  }
  Debug("ssl_ocsp", "converted OCSP request to DER; length=%d", ocsp_der_len);

  if (ats_base64_encode(ocsp_der, ocsp_der_len, ocsp_encoded_der, MAX_OCSP_GET_ENCODED_LENGTH, &ocsp_encoded_der_len) == false ||
      ocsp_encoded_der_len == 0) {
    Error("stapling_refresh_response: unable to base64 encode OCSP DER; falling back to POST; url=%s", base_url);
    OPENSSL_free(ocsp_der);
    return nullptr;
  }
  OPENSSL_free(ocsp_der);
  Debug("ssl_ocsp", "encoded DER with base64: %s", ocsp_encoded_der);

  if (nullptr == Encoding::pure_escapify_url(nullptr, ocsp_encoded_der, ocsp_encoded_der_len, &ocsp_escaped_len, ocsp_escaped,
                                             MAX_OCSP_GET_ENCODED_LENGTH, encoding_map)) {
    Error("stapling_refresh_response: unable to escapify encoded url; falling back to POST; url=%s", base_url);
    return nullptr;
  }
  Debug("ssl_ocsp", "escaped encoded path; %d bytes, %s", ocsp_escaped_len, ocsp_escaped);

  size_t total_url_len =
    sizeof(char) * (strlen(base_url) + 1 + ocsp_escaped_len + 1); // <base URL> + / + <encoded OCSP request> + \0
  unsigned int buffer_idx  = DEFAULT_SMALL_BUFFER_SIZE;           // idx 2, aka BUFFER_SIZE_INDEX_512 should be enough in most cases
  unsigned int buffer_size = BUFFER_SIZE_FOR_INDEX(buffer_idx);

  // increase buffer index as necessary to fit the largest observed encoded_path_len
  while (buffer_size < total_url_len && buffer_idx < MAX_BUFFER_SIZE_INDEX) {
    buffer_size = BUFFER_SIZE_FOR_INDEX(++buffer_idx);
    Debug("ssl_ocsp", "increased buffer index to %d", buffer_idx);
  }

  if (buffer_size < total_url_len) {
    // this case should never occur; the largest index, 14, is 2MB
    Error("Unable to identify a buffer index large enough to fit %zu bytes for the the request url; maximum index %d with size %d",
          total_url_len, buffer_idx, buffer_size);
    return nullptr;
  }

  Debug("ssl_ocsp", "creating new buffer block with index %d, size %d, to store %zu encoded bytes", buffer_idx, buffer_size,
        total_url_len);
  url = new_IOBufferBlock();
  url->alloc(buffer_idx);

  int written = ink_strlcpy(url->end(), base_url, url->write_avail());
  url->fill(written);

  // Append '/' if base_url does not end with it
  if (url->buf()[url->size() - 1] != '/') {
    strncat(url->end(), "/", 1);
    url->fill(1);
  }

  written = ink_strlcat(url->end(), ocsp_escaped, url->write_avail());
  url->fill(written);
  Debug("ssl_ocsp", "appended encoded data to path: %s", url->buf());

  return url;
}

static bool
stapling_refresh_response(certinfo *cinf, TS_OCSP_RESPONSE **prsp)
{
  bool rv                    = true;
  TS_OCSP_REQUEST *req       = nullptr;
  TS_OCSP_CERTID *id         = nullptr;
  int response_status        = 0;
  IOBufferBlock *url_for_get = nullptr;
  const char *url            = nullptr; // Final URL to use
  bool use_get               = false;

  *prsp = nullptr;

  req = TS_OCSP_REQUEST_new();
  if (!req) {
    goto err;
  }
  id = TS_OCSP_CERTID_dup(cinf->cid);
  if (!id) {
    goto err;
  }
  if (!TS_OCSP_request_add0_id(req, id)) {
    goto err;
  }

  if (SSLConfigParams::ssl_ocsp_request_mode) { // True: GET, False: POST
    url_for_get = make_url_for_get(req, cinf->uri);
    if (url_for_get != nullptr) {
      url     = url_for_get->buf();
      use_get = true;
    }
  }
  if (url == nullptr) {
    // GET request is disabled or the request is too large for GET request
    url = cinf->uri;
  }

  Debug("ssl_ocsp", "stapling_refresh_response: querying responder; method=%s uri=%s", use_get ? "GET" : "POST", cinf->uri);

  *prsp = query_responder(url, cinf->user_agent, req, SSLConfigParams::ssl_ocsp_request_timeout, use_get);
  if (*prsp == nullptr) {
    goto err;
  }

  response_status = ASN1_ENUMERATED_get((*prsp)->responseStatus);
  if (response_status == TS_OCSP_RESPONSE_STATUS_SUCCESSFUL) {
    Debug("ssl_ocsp", "stapling_refresh_response: query response received");
    stapling_check_response(cinf, *prsp);
  } else {
    Error("stapling_refresh_response: responder response error; uri=%s method=%s response_status=%d", cinf->uri,
          use_get ? "GET" : "POST", response_status);
  }

  if (!stapling_cache_response(*prsp, cinf)) {
    Error("stapling_refresh_response: can not cache response");
  } else {
    Debug("ssl_ocsp", "stapling_refresh_response: successfully refreshed OCSP response");
  }
  goto done;

err:
  rv = false;
  Error("stapling_refresh_response: failed to refresh OCSP response");

done:
  if (req) {
    TS_OCSP_REQUEST_free(req);
  }
  if (*prsp) {
    TS_OCSP_RESPONSE_free(*prsp);
  }
  if (url_for_get) {
    url_for_get->free();
  }
  return rv;
}

void
ocsp_update()
{
  shared_SSL_CTX ctx;
  TS_OCSP_RESPONSE *resp = nullptr;
  time_t current_time;

  SSLCertificateConfig::scoped_config certLookup;
  const unsigned ctxCount = certLookup ? certLookup->count() : 0;

  Debug("ssl_ocsp", "updating OCSP data");
  for (unsigned i = 0; i < ctxCount; i++) {
    SSLCertContext *cc = certLookup->get(i);
    if (cc) {
      ctx = cc->getCtx();
      if (ctx) {
        certinfo *cinf    = nullptr;
        certinfo_map *map = stapling_get_cert_info(ctx.get());
        if (map) {
          // Walk over all certs associated with this CTX
          for (auto &iter : *map) {
            cinf = iter.second;
            ink_mutex_acquire(&cinf->stapling_mutex);
            current_time = time(nullptr);
            if (cinf->resp_derlen == 0 || cinf->is_expire || cinf->expire_time < current_time) {
              ink_mutex_release(&cinf->stapling_mutex);
              if (stapling_refresh_response(cinf, &resp)) {
                Debug("ssl_ocsp", "Successfully refreshed OCSP for %s certificate. url=%s", cinf->certname, cinf->uri);
                Metrics::increment(ssl_rsb.ocsp_refreshed_cert);
              } else {
                Error("Failed to refresh OCSP for %s certificate. url=%s", cinf->certname, cinf->uri);
                Metrics::increment(ssl_rsb.ocsp_refresh_cert_failure);
              }
            } else {
              ink_mutex_release(&cinf->stapling_mutex);
            }
          }
        }
      }
    }
  }
}

// RFC 6066 Section-8: Certificate Status Request
int
#ifndef OPENSSL_IS_BORINGSSL
ssl_callback_ocsp_stapling(SSL *ssl)
#else
ssl_callback_ocsp_stapling(SSL *ssl, void *)
#endif
{
  // Assume SSL_get_SSL_CTX() is the same as reaching into the ssl structure
  // Using the official call, to avoid leaking internal openssl knowledge
  // originally was, cinf = stapling_get_cert_info(ssl->ctx);
  certinfo_map *map = stapling_get_cert_info(SSL_get_SSL_CTX(ssl));
  if (map == nullptr) {
    Debug("ssl_ocsp", "ssl_callback_ocsp_stapling: failed to get certificate map");
    return SSL_TLSEXT_ERR_NOACK;
  }

  if (map->empty()) {
    Debug("ssl_ocsp", "ssl_callback_ocsp_stapling: certificate map empty");
    return SSL_TLSEXT_ERR_NOACK;
  }

  // Fetch the specific certificate used in this negotiation
  X509 *cert = SSL_get_certificate(ssl);
  if (!cert) {
    Error("ssl_callback_ocsp_stapling: failed to get certificate");
    return SSL_TLSEXT_ERR_NOACK;
  }

  certinfo *cinf = nullptr;
#ifndef OPENSSL_IS_BORINGSSL
  certinfo_map::iterator iter = map->find(cert);
  if (iter != map->end()) {
    cinf = iter->second;
  }
#else
  for (certinfo_map::iterator iter = map->begin(); iter != map->end(); ++iter) {
    X509 *key = iter->first;
    if (key == nullptr) {
      continue;
    }

    if (X509_cmp(key, cert) == 0) {
      cinf = iter->second;
      break;
    }
  }
#endif

  if (cinf == nullptr) {
    Error("ssl_callback_ocsp_stapling: failed to get certificate information for ssl=%p", ssl);
    return SSL_TLSEXT_ERR_NOACK;
  }

  ink_mutex_acquire(&cinf->stapling_mutex);
  time_t current_time = time(nullptr);
  if ((cinf->resp_derlen == 0 || cinf->is_expire) || (cinf->expire_time < current_time && !cinf->is_prefetched)) {
    ink_mutex_release(&cinf->stapling_mutex);
    Debug("ssl_ocsp", "ssl_callback_ocsp_stapling: failed to get certificate status for %s", cinf->certname);
    return SSL_TLSEXT_ERR_NOACK;
  } else {
    unsigned char *p = static_cast<unsigned char *>(OPENSSL_malloc(cinf->resp_derlen));
    memcpy(p, cinf->resp_der, cinf->resp_derlen);
    ink_mutex_release(&cinf->stapling_mutex);
    SSL_set_tlsext_status_ocsp_resp(ssl, p, cinf->resp_derlen);
    Debug("ssl_ocsp", "ssl_callback_ocsp_stapling: successfully got certificate status for %s", cinf->certname);
    Debug("ssl_ocsp", "is_prefetched:%d uri:%s", cinf->is_prefetched, cinf->uri);
    return SSL_TLSEXT_ERR_OK;
  }
}
