/*
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

// Helper functions for certificate data extraction
static std::string
get_x509_name_string(X509_NAME *name)
{
  if (!name) {
    return "";
  }

  BIO *bio = BIO_new(BIO_s_mem());
  if (!bio) {
    return "";
  }

  if (X509_NAME_print_ex(bio, name, 0, XN_FLAG_RFC2253) <= 0) {
    BIO_free(bio);
    return "";
  }

  char       *data   = nullptr;
  long        length = BIO_get_mem_data(bio, &data);
  std::string result;

  if (data && length > 0) {
    result.assign(data, length);
  }

  BIO_free(bio);
  return result;
}

static std::string
get_x509_serial_string(X509 *cert)
{
  if (!cert) {
    return "";
  }

  ASN1_INTEGER *serial = X509_get_serialNumber(cert);
  if (!serial) {
    return "";
  }

  BIO *bio = BIO_new(BIO_s_mem());
  if (!bio) {
    return "";
  }

  if (i2a_ASN1_INTEGER(bio, serial) <= 0) {
    BIO_free(bio);
    return "";
  }

  char       *data   = nullptr;
  long        length = BIO_get_mem_data(bio, &data);
  std::string result;

  if (data && length > 0) {
    result.assign(data, length);
  }

  BIO_free(bio);
  return result;
}

static std::string
get_x509_time_string(ASN1_TIME *time)
{
  if (!time) {
    return "";
  }

  BIO *bio = BIO_new(BIO_s_mem());
  if (!bio) {
    return "";
  }

  if (ASN1_TIME_print(bio, time) <= 0) {
    BIO_free(bio);
    return "";
  }

  char       *data   = nullptr;
  long        length = BIO_get_mem_data(bio, &data);
  std::string result;

  if (data && length > 0) {
    result.assign(data, length);
  }

  BIO_free(bio);
  return result;
}

static std::string
get_x509_pem_string(X509 *cert)
{
  if (!cert) {
    return "";
  }

  BIO *bio = BIO_new(BIO_s_mem());
  if (!bio) {
    return "";
  }

  if (PEM_write_bio_X509(bio, cert) <= 0) {
    BIO_free(bio);
    return "";
  }

  char       *data   = nullptr;
  long        length = BIO_get_mem_data(bio, &data);
  std::string result;

  if (data && length > 0) {
    result.assign(data, length);
  }

  BIO_free(bio);
  return result;
}

static std::string
get_x509_signature_string(X509 *cert)
{
  if (!cert) {
    return "";
  }

  const ASN1_BIT_STRING *sig = nullptr;
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
  X509_get0_signature(&sig, nullptr, cert);
#else
  sig = cert->signature;
#endif

  if (!sig) {
    return "";
  }

  BIO *bio = BIO_new(BIO_s_mem());
  if (!bio) {
    return "";
  }

  for (int i = 0; i < sig->length; i++) {
    if (BIO_printf(bio, "%02x", sig->data[i]) <= 0) {
      BIO_free(bio);
      return "";
    }
    if (i < sig->length - 1) {
      if (BIO_printf(bio, ":") <= 0) {
        BIO_free(bio);
        return "";
      }
    }
  }

  char       *data   = nullptr;
  long        length = BIO_get_mem_data(bio, &data);
  std::string result;

  if (data && length > 0) {
    result.assign(data, length);
  }

  BIO_free(bio);
  return result;
}

static std::vector<std::string>
get_x509_san_strings(X509 *cert, int san_type)
{
  std::vector<std::string> results;

  if (!cert) {
    return results;
  }

  GENERAL_NAMES *names = static_cast<GENERAL_NAMES *>(X509_get_ext_d2i(cert, NID_subject_alt_name, nullptr, nullptr));
  if (!names) {
    return results;
  }

  int num_names = sk_GENERAL_NAME_num(names);
  for (int i = 0; i < num_names; i++) {
    GENERAL_NAME *name = sk_GENERAL_NAME_value(names, i);
    if (!name || name->type != san_type) {
      continue;
    }

    switch (san_type) {
    case GEN_DNS:
    case GEN_EMAIL:
    case GEN_URI: {
      ASN1_STRING *str = name->d.ia5;
      if (str) {
        const unsigned char *data = ASN1_STRING_get0_data(str);
        int                  len  = ASN1_STRING_length(str);
        if (data && len > 0) {
          results.emplace_back(reinterpret_cast<const char *>(data), len);
        }
      }
      break;
    }
    case GEN_IPADD: {
      ASN1_OCTET_STRING *ip = name->d.iPAddress;
      if (ip) {
        const unsigned char *data = ASN1_STRING_get0_data(ip);
        int                  len  = ASN1_STRING_length(ip);
        char                 ip_str[INET6_ADDRSTRLEN];

        if (len == 4) { // IPv4
          if (inet_ntop(AF_INET, data, ip_str, sizeof(ip_str)) != nullptr) {
            results.emplace_back(ip_str);
          }
        } else if (len == 16) { // IPv6
          if (inet_ntop(AF_INET6, data, ip_str, sizeof(ip_str)) != nullptr) {
            results.emplace_back(ip_str);
          }
        }
        break;
      }
    }
    }
  }
  GENERAL_NAMES_free(names);
  return results;
}
