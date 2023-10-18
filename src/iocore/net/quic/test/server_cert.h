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

#pragma once

static constexpr char server_crt[] = "-----BEGIN CERTIFICATE-----\n"
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

static constexpr char server_key[] = "-----BEGIN RSA PRIVATE KEY-----\n"
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
