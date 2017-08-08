/** @file
 *
 *  A brief file description
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include "catch.hpp"

#include <iostream>
#include <fstream>
#include <iomanip>

#ifdef OPENSSL_IS_BORINGSSL
#include <openssl/base.h>
#endif

#include <openssl/ssl.h>

#include "QUICCrypto.h"

const static uint32_t MAX_HANDSHAKE_MSG_LEN = 2048;

static const char server_crt[] = "-----BEGIN CERTIFICATE-----\n"
                                 "MIIDRjCCAi4CCQDoLSBwQxmcJTANBgkqhkiG9w0BAQsFADBlMQswCQYDVQQGEwJK\n"
                                 "UDEOMAwGA1UECBMFVG9reW8xDzANBgNVBAcTBk1pbmF0bzEhMB8GA1UEChMYSW50\n"
                                 "ZXJuZXQgV2lkZ2l0cyBQdHkgTHRkMRIwEAYDVQQDEwlsb2NhbGhvc3QwHhcNMTcw\n"
                                 "MTE4MDEyMzA3WhcNMjcwMTE2MDEyMzA3WjBlMQswCQYDVQQGEwJKUDEOMAwGA1UE\n"
                                 "CBMFVG9reW8xDzANBgNVBAcTBk1pbmF0bzEhMB8GA1UEChMYSW50ZXJuZXQgV2lk\n"
                                 "Z2l0cyBQdHkgTHRkMRIwEAYDVQQDEwlsb2NhbGhvc3QwggEiMA0GCSqGSIb3DQEB\n"
                                 "AQUAA4IBDwAwggEKAoIBAQC70j62KOWkuqNsDhl+7uqKFS6TMcJYLdYrH1YInwlY\n"
                                 "htOMSMWx2hPSYYBKzVQpLvhe2LPbhLwcVJdq4aqQNjNpxrpxW/YIY5zxCRVgQsgf\n"
                                 "KXiKgUR0G+F3MQHsm1YIqxQU2OeJldIZUBM2YMDp8h1CXTAvGaAZaXsqO9UvR2Zw\n"
                                 "JZJ+GElYNlNwhdStqIM8v1JNFjfO3gWkVqTv+QM4fmpror2pp8CaDrueg4PrSY3Y\n"
                                 "D/WG75rkmlrW26t0Q8fjkn+s/UiQ3V/IkP1+MfrJWH6RL2DGjBv2KfNAik42xWUi\n"
                                 "KXzaNcDFN4hjqVG59O9bPnUDn1wPypY/TXB4iqSAlxupAgMBAAEwDQYJKoZIhvcN\n"
                                 "AQELBQADggEBAKLc+P5YfusNYIkX3YE+gHBVpo95xnoVUcsGr/h1zanCkmsyKkNU\n"
                                 "e2w9xsVnRLgpRfwrnwiaNP/k6cPYt5ePPCJjUfkO7Ql7DCcjLgEp8lrvxMmRIdSg\n"
                                 "LPq+NdityxXYhfaZdGdXjnLLiq3zYL/8aYjjZ8YAZTuu6pBgfGvjcqYLV1ohimrP\n"
                                 "8BW0BbnvedqTyL7tdKjdiWnHE5ObrxnphL2evoStskBr5CLYR4vX7+qp0oVSz2Ol\n"
                                 "nBMV3wXyhHBY1tuT1SK7ajC/ZHrciZosACRV5PC6nKXi3shWOxt76SZV3HcMmFwX\n"
                                 "NQYYTBOlb5U080adFSmP5/6NRzrKwZ3mD2s=\n"
                                 "-----END CERTIFICATE-----\n";

static const char server_key[] = "-----BEGIN RSA PRIVATE KEY-----\n"
                                 "MIIEpAIBAAKCAQEAu9I+tijlpLqjbA4Zfu7qihUukzHCWC3WKx9WCJ8JWIbTjEjF\n"
                                 "sdoT0mGASs1UKS74Xtiz24S8HFSXauGqkDYzaca6cVv2CGOc8QkVYELIHyl4ioFE\n"
                                 "dBvhdzEB7JtWCKsUFNjniZXSGVATNmDA6fIdQl0wLxmgGWl7KjvVL0dmcCWSfhhJ\n"
                                 "WDZTcIXUraiDPL9STRY3zt4FpFak7/kDOH5qa6K9qafAmg67noOD60mN2A/1hu+a\n"
                                 "5Jpa1turdEPH45J/rP1IkN1fyJD9fjH6yVh+kS9gxowb9inzQIpONsVlIil82jXA\n"
                                 "xTeIY6lRufTvWz51A59cD8qWP01weIqkgJcbqQIDAQABAoIBADI3ShEF6jAavmq7\n"
                                 "clGfqxF0DFnKaf2Nc79fx27SpnsGwTS2mDSu67HJ47UcJK5GIp2pLp04ZdrlOv6W\n"
                                 "izW3aBOV0G9SePtRNrqzBQYRlNPQEKxnV1f7xFJLxgnulhgHNX1FaNI+PkgKQri9\n"
                                 "MZba5rvBkoplPYrNyuJF0P+tBVRiISWDY00PlZ57pQDyOvXzUckAkxmjNzo+86ld\n"
                                 "/NyO+nR45vVKSeIBT5tT67D8wRisZgO/7QKP5sbKYwa7AR4sTEYFwBaFi4Mr6v1T\n"
                                 "kp0KxOFBI+MioFwyK7ZjkoKClrY/K0IPsKfn2vmi6jLpfkA+qCl1JsVhrfVO3KJc\n"
                                 "PXXF4QECgYEA9339GQS2AWSuA/9ZgHFqTTOEEHujCkh9D4mKO4LRi5hKPN9NQKUU\n"
                                 "KgaBXWTbr8FwOTXw6HMl0SaIOdc6VxdzViNvPCpu2Wn8hyTC5Mjs/BtXkXNcBQqs\n"
                                 "tPm0JxgC6fpQAb+gU+zZ+QQNlUWH/CEiQFxxGNzBn9E3Xq2j0StdhPECgYEAwkci\n"
                                 "GiQuM4KMDdwbs4RDlEZyvXxWwgHKPoXv/Uq7HXtuT1FGb/+Rf3BGimMf2Qqmppp8\n"
                                 "MAZ+xk+eXhtqKZHsV2ifhUfuVZ6NPhT2WRyn6MozuHh3MK4l2KtOhxulcoX/2sDk\n"
                                 "dLYclxhXZFuXvbLz2KpgMmPMGyzEQNHQaoTkojkCgYEAxb/wVGY0OybD+EO2su9s\n"
                                 "PaVU94qielvzOU/vmJ9taTnUz5Co/Gcqlm2+Pe6RrnxEfCICjOk8pUJBhN3ZKq99\n"
                                 "I62Keqt5CNUrxpvz8bQtzz7VmE1xkEG4P55pePcxlNzBwrPnmkdc3yCC7euxvR6I\n"
                                 "bJ6wa2owd89Gi6r4gvBAeDECgYBpdiPU/P73h05v16RR9uKYgwWWRwDxn/chqaN1\n"
                                 "ZDPe9ToUZJJQCfP5sgEY7mZDc7yzg/kWOPBoxp+5hjhDCKu7Z1fxCfMfF0qlAMwZ\n"
                                 "46xieiFJaluJWX/B9nxSa3eMi6EwJrXdhV5Pxy7pk67zk0k7vIEr2XDa75o5dawl\n"
                                 "pq5WQQKBgQC9xsRLtQjnDEdNEgCicTupa7BXmvc9tRb1mA5SeqjwzYuulrTyvn5Y\n"
                                 "QOXYdz8aeZ+ZQ/cDeGA3jA6lekWnExkp9enHeqadyDWM7rvXi800E6gB/vrO7r/c\n"
                                 "iE+fpXud6cwNw2XYsk6RBSQ8qhJoCpa+koPXfSJOZ9Y89NMbtq0w3Q==\n"
                                 "-----END RSA PRIVATE KEY-----\n";

void
print_hex(const uint8_t *v, size_t len)
{
  for (size_t i = 0; i < len; i++) {
    std::cout << std::setw(2) << std::setfill('0') << std::hex << static_cast<uint32_t>(v[i]) << " ";

    if (i != 0 && (i + 1) % 32 == 0 && i != len - 1) {
      std::cout << std::endl;
    }
  }

  std::cout << std::endl;

  return;
}

TEST_CASE("QUICCrypto 1-RTT", "[quic]")
{
  // Client
  SSL_CTX *client_ssl_ctx = SSL_CTX_new(TLS_method());
  SSL_CTX_set_min_proto_version(client_ssl_ctx, TLS1_3_VERSION);
  SSL_CTX_set_max_proto_version(client_ssl_ctx, TLS1_3_VERSION);
  QUICCrypto *client = new QUICCrypto(client_ssl_ctx, NET_VCONNECTION_OUT);

  // Server
  SSL_CTX *server_ssl_ctx = SSL_CTX_new(TLS_method());
  SSL_CTX_set_min_proto_version(server_ssl_ctx, TLS1_3_VERSION);
  SSL_CTX_set_max_proto_version(server_ssl_ctx, TLS1_3_VERSION);
  BIO *crt_bio(BIO_new_mem_buf(server_crt, sizeof(server_crt)));
  SSL_CTX_use_certificate(server_ssl_ctx, PEM_read_bio_X509(crt_bio, nullptr, nullptr, nullptr));
  BIO *key_bio(BIO_new_mem_buf(server_key, sizeof(server_key)));
  SSL_CTX_use_PrivateKey(server_ssl_ctx, PEM_read_bio_PrivateKey(key_bio, nullptr, nullptr, nullptr));
  QUICCrypto *server = new QUICCrypto(server_ssl_ctx, NET_VCONNECTION_IN);

  // Client Hello
  uint8_t client_hello[MAX_HANDSHAKE_MSG_LEN] = {0};
  size_t client_hello_len                     = 0;
  CHECK(client->handshake(client_hello, client_hello_len, MAX_HANDSHAKE_MSG_LEN, nullptr, 0));
  std::cout << "Client Hello" << std::endl;
  print_hex(client_hello, client_hello_len);

  // Server Hello
  uint8_t server_hello[MAX_HANDSHAKE_MSG_LEN] = {0};
  size_t server_hello_len                     = 0;
  CHECK(server->handshake(server_hello, server_hello_len, MAX_HANDSHAKE_MSG_LEN, client_hello, client_hello_len));
  std::cout << "Server Hello" << std::endl;
  print_hex(server_hello, server_hello_len);

  // Client Fnished
  uint8_t client_finished[MAX_HANDSHAKE_MSG_LEN] = {0};
  size_t client_finished_len                     = 0;
  CHECK(client->handshake(client_finished, client_finished_len, MAX_HANDSHAKE_MSG_LEN, server_hello, server_hello_len));
  std::cout << "Client Finished" << std::endl;
  print_hex(client_finished, client_finished_len);

  // Post Handshake Msg
  uint8_t post_handshake_msg[MAX_HANDSHAKE_MSG_LEN] = {0};
  size_t post_handshake_msg_len                     = 0;
  CHECK(server->handshake(post_handshake_msg, post_handshake_msg_len, MAX_HANDSHAKE_MSG_LEN, client_finished, client_finished_len));
  std::cout << "Post Handshake Message" << std::endl;
  print_hex(post_handshake_msg, post_handshake_msg_len);

  CHECK(client->setup_session());
  CHECK(server->setup_session());

  // encrypt - decrypt
  uint8_t original[] = {
    0x41, 0x70, 0x61, 0x63, 0x68, 0x65, 0x20, 0x54, 0x72, 0x61, 0x66, 0x66, 0x69, 0x63, 0x20, 0x53,
    0x65, 0x72, 0x76, 0x65, 0x72, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  };
  uint64_t pkt_num = 0x123456789;
  uint8_t ad[]     = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};

  // client (encrypt) - server (decrypt)
  std::cout << "Original Text" << std::endl;
  print_hex(original, sizeof(original));

  uint8_t cipher[128] = {0}; // >= original len + EVP_AEAD_max_overhead
  size_t cipher_len   = 0;
  CHECK(client->encrypt(cipher, cipher_len, sizeof(cipher), original, sizeof(original), pkt_num, ad, sizeof(ad),
                        QUICKeyPhase::PHASE_0));

  std::cout << "Encrypted Text" << std::endl;
  print_hex(cipher, cipher_len);

  uint8_t plain[128] = {0};
  size_t plain_len   = 0;
  CHECK(server->decrypt(plain, plain_len, sizeof(plain), cipher, cipher_len, pkt_num, ad, sizeof(ad), QUICKeyPhase::PHASE_0));

  std::cout << "Decrypted Text" << std::endl;
  print_hex(plain, plain_len);

  CHECK(sizeof(original) == (plain_len));
  CHECK(memcmp(original, plain, plain_len) == 0);

  // Teardown
  delete client;
  delete server;
}
