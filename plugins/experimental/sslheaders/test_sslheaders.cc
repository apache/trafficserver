/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "sslheaders.h"
#include "tscore/TestBox.h"
#include <cstdio>
#include <cstdarg>
#include <openssl/ssl.h>
#include <openssl/bio.h>
#include <openssl/pem.h>

char *
_TSstrdup(const char *str, int64_t length, const char *)
{
  if (length == -1) {
    return strdup(str);
  } else {
    return strndup(str, length);
  }
}

void
_TSfree(void *ptr)
{
  free(ptr);
}

void
TSDebug(const char *tag, const char *fmt, ...)
{
  va_list args;

  fprintf(stderr, "%s", tag);
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  fprintf(stderr, "\n");
}

void
TSError(const char *fmt, ...)
{
  va_list args;

  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  fprintf(stderr, "\n");
}

REGRESSION_TEST(ParseExpansion)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);

#define EXPECT_TRUE(expression, _name, _scope, _field)                                                                            \
  do {                                                                                                                            \
    SslHdrExpansion exp;                                                                                                          \
    box.check(SslHdrParseExpansion(expression, exp) == true, "'%s' failed (expected success)", (expression));                     \
    box.check(strcmp(exp.name.c_str(), _name) == 0, "'%s' expected name %s, received %s", (expression), (_name),                  \
              exp.name.c_str());                                                                                                  \
    box.check(exp.scope == (_scope), "'%s' expected scope 0x%x (%s), received 0x%x", (expression), (_scope), #_scope, exp.scope); \
    box.check(exp.field == (_field), "'%s' expected field 0x%x (%s), received 0x%x", (expression), (_field), #_field, exp.field); \
  } while (0)

#define EXPECT_FALSE(expression)                                                                                  \
  do {                                                                                                            \
    SslHdrExpansion exp;                                                                                          \
    box.check(SslHdrParseExpansion(expression, exp) == false, "'%s' succeeded (expected failure)", (expression)); \
  } while (0)

  box = REGRESSION_TEST_PASSED;

  EXPECT_FALSE("");
  EXPECT_FALSE("missing-certificate-selector");
  EXPECT_FALSE("missing-field-selector=");
  EXPECT_FALSE("missing-field-selector=client");
  EXPECT_FALSE("missing-field-selector=client.");

  EXPECT_TRUE("ssl-client-cert=client.certificate", "ssl-client-cert", SSL_HEADERS_SCOPE_CLIENT, SSL_HEADERS_FIELD_CERTIFICATE);
  EXPECT_TRUE("ssl-server-signature=server.signature", "ssl-server-signature", SSL_HEADERS_SCOPE_SERVER,
              SSL_HEADERS_FIELD_SIGNATURE);

  EXPECT_TRUE("certificate=server.certificate", "certificate", SSL_HEADERS_SCOPE_SERVER, SSL_HEADERS_FIELD_CERTIFICATE);
  EXPECT_TRUE("subject=server.subject", "subject", SSL_HEADERS_SCOPE_SERVER, SSL_HEADERS_FIELD_SUBJECT);
  EXPECT_TRUE("issuer=server.issuer", "issuer", SSL_HEADERS_SCOPE_SERVER, SSL_HEADERS_FIELD_ISSUER);
  EXPECT_TRUE("serial=server.serial", "serial", SSL_HEADERS_SCOPE_SERVER, SSL_HEADERS_FIELD_SERIAL);
  EXPECT_TRUE("signature=server.signature", "signature", SSL_HEADERS_SCOPE_SERVER, SSL_HEADERS_FIELD_SIGNATURE);
  EXPECT_TRUE("notbefore=server.notbefore", "notbefore", SSL_HEADERS_SCOPE_SERVER, SSL_HEADERS_FIELD_NOTBEFORE);
  EXPECT_TRUE("notafter=server.notafter", "notafter", SSL_HEADERS_SCOPE_SERVER, SSL_HEADERS_FIELD_NOTAFTER);

#undef EXPECT_FALSE
#undef EXPECT_TRUE
}

// Certificate:
//     Data:
//         Version: 3 (0x2)
//         Serial Number: 16125629757001825863 (0xdfc9bed3a58ffe47)
//     Signature Algorithm: sha1WithRSAEncryption
//         Issuer: CN=test.sslheaders.trafficserver.apache.org
//         Validity
//             Not Before: Jul 23 17:51:08 2014 GMT
//             Not After : May 12 17:51:08 2017 GMT
//         Subject: CN=test.sslheaders.trafficserver.apache.org
//         Subject Public Key Info:
//             Public Key Algorithm: rsaEncryption
//                 Public-Key: (1024 bit)
//                 Modulus:
//                     00:cd:ba:29:dc:57:9e:a2:30:0d:44:ed:2b:3d:06:
//                     53:6f:46:65:1d:57:70:27:e5:2e:af:5c:73:ff:85:
//                     74:95:4d:28:fe:de:8d:08:ed:eb:3f:da:7a:01:33:
//                     b5:26:5d:64:c1:18:d8:dc:41:8c:c1:79:df:d0:22:
//                     fa:8c:f6:9e:50:e0:1e:e4:28:54:db:d7:10:4e:97:
//                     81:14:dc:b1:e5:f5:fc:f3:87:16:d9:30:07:36:30:
//                     75:b9:5f:cf:9e:09:1e:8a:e8:80:6e:e6:c4:6e:2d:
//                     33:ef:21:98:60:eb:7f:df:7e:13:49:4c:89:b2:5b:
//                     6f:9e:1f:c8:2e:54:67:77:f1
//                 Exponent: 65537 (0x10001)
//         X509v3 extensions:
//             X509v3 Subject Alternative Name:
//                 DNS:test.sslheaders.trafficserver.apache.org
//     Signature Algorithm: sha1WithRSAEncryption
//          26:b2:1d:1c:39:7b:48:9e:8c:d9:22:80:b0:11:93:d6:91:5a:
//          2c:b4:58:59:14:75:f7:e1:cb:08:e7:38:ac:44:1a:f7:d9:1a:
//          43:50:3c:53:7e:d1:21:e4:ee:b0:26:f1:29:73:b4:e2:04:95:
//          2b:f1:ff:2f:43:07:29:f8:21:e4:b0:d9:a5:3a:cd:98:99:51:
//          23:e2:f5:2b:60:f3:fb:56:bf:d3:2f:39:25:3f:27:b0:87:68:
//          79:16:b9:86:df:05:30:4d:0e:89:1f:a8:5b:6a:63:75:09:ec:
//          f9:fe:eb:26:d2:d9:16:73:c2:64:a3:8a:74:fc:1a:09:44:df:
//          42:51

// Given a PEM formatted object, remove the newlines to get what we would
// see in a HTTP header.
static char *
make_pem_header(const char *pem)
{
  char *hdr;
  char *ptr;
  unsigned remain;

  hdr = ptr = strdup(pem);
  remain    = strlen(hdr);

  for (char *nl; (nl = (char *)memchr(ptr, '\n', remain)); ptr = nl) {
    *nl = ' ';
    remain -= nl - ptr;
  }

  return hdr;
}

REGRESSION_TEST(ParseX509Fields)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  // A self-signed certificate for CN=test.sslheaders.trafficserver.apache.org.
  static const char *test_certificate = "-----BEGIN CERTIFICATE-----\n"
                                        "MIICGzCCAYSgAwIBAgIJAN/JvtOlj/5HMA0GCSqGSIb3DQEBBQUAMDMxMTAvBgNV\n"
                                        "BAMMKHRlc3Quc3NsaGVhZGVycy50cmFmZmljc2VydmVyLmFwYWNoZS5vcmcwHhcN\n"
                                        "MTQwNzIzMTc1MTA4WhcNMTcwNTEyMTc1MTA4WjAzMTEwLwYDVQQDDCh0ZXN0LnNz\n"
                                        "bGhlYWRlcnMudHJhZmZpY3NlcnZlci5hcGFjaGUub3JnMIGfMA0GCSqGSIb3DQEB\n"
                                        "AQUAA4GNADCBiQKBgQDNuincV56iMA1E7Ss9BlNvRmUdV3An5S6vXHP/hXSVTSj+\n"
                                        "3o0I7es/2noBM7UmXWTBGNjcQYzBed/QIvqM9p5Q4B7kKFTb1xBOl4EU3LHl9fzz\n"
                                        "hxbZMAc2MHW5X8+eCR6K6IBu5sRuLTPvIZhg63/ffhNJTImyW2+eH8guVGd38QID\n"
                                        "AQABozcwNTAzBgNVHREELDAqgih0ZXN0LnNzbGhlYWRlcnMudHJhZmZpY3NlcnZl\n"
                                        "ci5hcGFjaGUub3JnMA0GCSqGSIb3DQEBBQUAA4GBACayHRw5e0iejNkigLARk9aR\n"
                                        "Wiy0WFkUdffhywjnOKxEGvfZGkNQPFN+0SHk7rAm8SlztOIElSvx/y9DByn4IeSw\n"
                                        "2aU6zZiZUSPi9Stg8/tWv9MvOSU/J7CHaHkWuYbfBTBNDokfqFtqY3UJ7Pn+6ybS\n"
                                        "2RZzwmSjinT8GglE30JR\n"
                                        "-----END CERTIFICATE-----\n";
#if 0
  "-----BEGIN RSA PRIVATE KEY-----"
  "MIICXgIBAAKBgQDNuincV56iMA1E7Ss9BlNvRmUdV3An5S6vXHP/hXSVTSj+3o0I"
  "7es/2noBM7UmXWTBGNjcQYzBed/QIvqM9p5Q4B7kKFTb1xBOl4EU3LHl9fzzhxbZ"
  "MAc2MHW5X8+eCR6K6IBu5sRuLTPvIZhg63/ffhNJTImyW2+eH8guVGd38QIDAQAB"
  "AoGBAJLTO48DhbbxHndD4SkTe7aeAgpX3jbK7W/ARxVldNgdkpWb1gI6czxGO+7h"
  "rXatDvx1NEi2C7QFvEN6w2CZnlCIEYLdC3JPA9qQXD66vHSVttNqwLHezm+tf3Ci"
  "DgPoSWABHJbDc/TFHjeVDvzkGJ/x0E6CO8lMvvDRbzjcNRoBAkEA80ulSvbCpZHL"
  "aTqMwB/djvEFyrlyDyD8WkJewkL2q7HRWimNTAU+AsYftzn9kVaIHcVC3x1T47bB"
  "qP1yEn+eoQJBANh4TtlZOEX6ykm4KqrCQXzOU5sp3m0RmqzYGQ3g8+8X8VTHjduw"
  "OoJ/vJo6peluh0JalDbdSkCHU0OiILYD51ECQEoEP3s46yq32ixfVaa1ixALn3l3"
  "RY34uQ00l+N9v9GoPUqyzXvNNHpfkBKMH+pxauOzuY5rO7RRS0WAJY4fKUECQQCd"
  "R6R6lTGm3tYVhAM0OJoeVUc3yM78Tjsk9IoXpGd4Q9wrriYrBbstUCQ3pv8fQRhz"
  "pJ5l0pj9k5Vy4ZyEwwdRAkEA3WViCDYe+uxeXcJxqiRHFoGm7YvkqcpBk9UQaWiz"
  "d9D304LUJ+dfMHNUmhBe/HKG35VU8dG5/0E9vkQyz99zCw=="
  "-----END RSA PRIVATE KEY-----"
  ;
#endif

  TestBox box(t, pstatus);

  box = REGRESSION_TEST_PASSED;

  BIO *exp   = BIO_new(BIO_s_mem());
  BIO *bio   = BIO_new_mem_buf((void *)test_certificate, -1);
  X509 *x509 = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);

  box.check(x509 != nullptr, "failed to load the test certificate");

#define EXPECT_FIELD(_field, _value)                                                                                    \
  do {                                                                                                                  \
    long len;                                                                                                           \
    char *ptr;                                                                                                          \
    SslHdrExpandX509Field(exp, x509, _field);                                                                           \
    len = BIO_get_mem_data(exp, &ptr);                                                                                  \
    box.check(strncmp(_value, ptr, len) == 0, "expected '%s' for %s, received '%.*s'", _value, #_field, (int)len, ptr); \
  } while (0)

  // Munge the PEM certificate to what we expect in the HTTP header.
  char *certhdr = make_pem_header(test_certificate);

  EXPECT_FIELD(SSL_HEADERS_FIELD_NONE, "");
  EXPECT_FIELD(SSL_HEADERS_FIELD_CERTIFICATE, certhdr);
  EXPECT_FIELD(SSL_HEADERS_FIELD_SUBJECT, "CN = test.sslheaders.trafficserver.apache.org");
  EXPECT_FIELD(SSL_HEADERS_FIELD_ISSUER, "CN = test.sslheaders.trafficserver.apache.org");
  EXPECT_FIELD(SSL_HEADERS_FIELD_SERIAL, "DFC9BED3A58FFE47");
  EXPECT_FIELD(SSL_HEADERS_FIELD_SIGNATURE, "26B21D1C397B489E8CD92280B01193D6915A"
                                            "2CB458591475F7E1CB08E738AC441AF7D91A"
                                            "43503C537ED121E4EEB026F12973B4E20495"
                                            "2BF1FF2F430729F821E4B0D9A53ACD989951"
                                            "23E2F52B60F3FB56BFD32F39253F27B08768"
                                            "7916B986DF05304D0E891FA85B6A637509EC"
                                            "F9FEEB26D2D91673C264A38A74FC1A0944DF"
                                            "4251");

  EXPECT_FIELD(SSL_HEADERS_FIELD_NOTBEFORE, "Jul 23 17:51:08 2014 GMT");
  EXPECT_FIELD(SSL_HEADERS_FIELD_NOTAFTER, "May 12 17:51:08 2017 GMT");

#undef EXPECT_FIELD

  BIO_free(exp);
  BIO_free(bio);
  free(certhdr);
}

int
main(int argc, const char **argv)
{
  SSL_library_init();
  return RegressionTest::main(argc, argv, REGRESSION_TEST_QUICK);
}
