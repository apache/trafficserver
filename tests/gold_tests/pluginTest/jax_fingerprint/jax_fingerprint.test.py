'''
Verify the behavior of the jax_fingerprint plugin.

Covers all supported run modes (overwrite, keep, append), setup types
(global, remap, hybrid), fingerprint methods (JA3, JA4, JA4H), and both
HTTP/1.1 and HTTP/2 client connections.

Global setup:   plugin in plugin.config, applies to every request.
Remap setup:    plugin per remap rule, applies only to matched routes.
Hybrid setup:   global plugin captures the TLS client hello (creates
                context) while a remap plugin reads that context and
                sets headers only on matched routes.
'''
#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information
#  regarding copyright ownership.  The ASF licenses this file
#  to you under the Apache License, Version 2.0 (the
#  "License"); you may not use this file except in compliance
#  with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

import os

Test.Summary = __doc__
Test.SkipUnless(Condition.PluginExists('jax_fingerprint.so'))
exec(open(os.path.join(Test.TestDirectory, 'jax_fingerprint_common.py')).read())

# ======================================================================
# Test instances
# ======================================================================

# --- Global setup -------------------------------------------------------

# All HTTP/1.1 requests receive a JA4H fingerprint header (overwrite mode).
JaxFingerprintTest('Global JA4H overwrite', 'JA4H', 'global')

# All TLS requests receive a JA3 fingerprint header (overwrite mode).
JaxFingerprintTest('Global JA3 overwrite', 'JA3', 'global')

# --- Remap setup --------------------------------------------------------

# Only requests matching the remap rule receive a JA4H header.
JaxFingerprintTest('Remap JA4H', 'JA4H', 'remap')

# Remap plugin with standalone captures TLS client hellos globally and
# sets JA3 headers only on the matched route.
JaxFingerprintTest('Remap JA3 standalone', 'JA3', 'remap')

# --- Hybrid setup -------------------------------------------------------

# Global plugin captures TLS client hellos (creates vconn context).
# Remap plugin reads that shared context and sets headers on matched routes.
JaxFingerprintTest('Hybrid JA4', 'JA4', 'hybrid')

# --- HTTP/2 (h2 over TLS) -----------------------------------------------

# JA4: the h2 ALPN in the ClientHello produces a different fingerprint than
# the HTTP/1.1 case – exercises the full TLS extension path with h2 ALPN.
JaxFingerprintTest('Global JA4 HTTP/2', 'JA4', 'global', http2=True)

# JA4H: exercises the HTTP/2 branch of get_version() which detects h2 via
# TSHttpTxnClientProtocolStackContains.
JaxFingerprintTest('Global JA4H HTTP/2', 'JA4H', 'global', http2=True)

# keep mode: existing x-jax / x-jax-via headers are not overwritten.
JaxFingerprintTest('Global JA4H keep mode', 'JA4H', 'global', mode='keep')

# append mode: fingerprint / proxy name are appended to existing header values.
JaxFingerprintTest('Global JA4H append mode', 'JA4H', 'global', mode='append')

# --- SNI allowlist (servernames) ----------------------------------------

# servernames restricts fingerprinting to connections whose TLS SNI is in
# the list.  Connections with a non-matching SNI are skipped at the client-
# hello hook: no context is created and handle_read_request_hdr is a no-op,
# so neither the fingerprint header nor the via header are set.
JaxFingerprintTest('Global JA4 servernames', 'JA4', 'global', servernames='jax.server.test')

# --- Hybrid + SNI allowlist (most common production pattern) ---------------

# Global plugin captures TLS client hellos for allowed SNIs only.
# Remap plugin sets headers on both routes, but only the SNI-allowed
# connection has a vconn context, so only that request gets headers.
JaxFingerprintTest('Hybrid JA4 servernames', 'JA4', 'hybrid', servernames='jax.server.test')

# --- Custom log field (log_field) -------------------------------------------

# Register a custom log field via log_field and verify the fingerprint
# appears in the ATS access log configured in logging.yaml.
JaxFingerprintTest('Global JA4H log-field', 'JA4H', 'global', log_field='jaxja4h')
JaxFingerprintTest('Global JA4 log-field', 'JA4', 'global', log_field='jaxja4')

# --- All Methods Test --------------------------------------------------------

# Multiple methods loaded simultaneously, verifying shared context map works.
AllMethodsTest('Multiple methods loaded simultaneously')
