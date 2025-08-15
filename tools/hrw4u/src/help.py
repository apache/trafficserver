#
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

from __future__ import annotations

#
# LSP Documentation Tables
#

# LSP-specific metadata for header contexts
LSP_HEADER_CONTEXTS = {
    "inbound.req":
        {
            "name": "Inbound Request Headers",
            "description":
                "Use this prefix to access HTTP headers in the inbound request. Follow with a dot and the header name (e.g., `inbound.req.Content-Type`).",
            "direction": "inbound",
            "type": "request"
        },
    "inbound.resp":
        {
            "name": "Inbound Response Headers",
            "description":
                "Use this prefix to access HTTP headers in the inbound response. Follow with a dot and the header name (e.g., `inbound.resp.Content-Type`).",
            "direction": "inbound",
            "type": "response"
        },
    "outbound.req":
        {
            "name": "Outbound Request Headers",
            "description":
                "Use this prefix to access HTTP headers in the outbound request. Follow with a dot and the header name (e.g., `outbound.req.Content-Type`).",
            "direction": "outbound",
            "type": "request"
        },
    "outbound.resp":
        {
            "name": "Outbound Response Headers",
            "description":
                "Use this prefix to access HTTP headers in the outbound response. Follow with a dot and the header name (e.g., `outbound.resp.Content-Type`).",
            "direction": "outbound",
            "type": "response"
        },
    "inbound.cookie":
        {
            "name": "Inbound Request Cookies",
            "description":
                "Use this prefix to access HTTP cookies in the inbound request. Follow with a dot and the cookie name (e.g., `inbound.cookie.sessionid`).",
            "direction": "inbound",
            "type": "cookie"
        },
    "outbound.cookie":
        {
            "name": "Outbound Request Cookies",
            "description":
                "Use this prefix to access HTTP cookies in the outbound request. Follow with a dot and the cookie name (e.g., `outbound.cookie.sessionid`).",
            "direction": "outbound",
            "type": "cookie"
        },
    "inbound.url":
        {
            "name": "Inbound Request URL Components",
            "description":
                "Use this prefix to access URL components of the inbound request. Available: host, port, path, query, scheme, fragment.",
            "direction": "inbound",
            "type": "url",
            "components": ["host", "port", "path", "query", "scheme", "fragment"]
        },
    "outbound.url":
        {
            "name": "Outbound Request URL Components",
            "description":
                "Use this prefix to access URL components of the outbound request. Available: host, port, path, query, scheme, fragment.",
            "direction": "outbound",
            "type": "url",
            "components": ["host", "port", "path", "query", "scheme", "fragment"]
        },
    "inbound.method":
        {
            "name": "Inbound HTTP Method",
            "description": "The HTTP method of the inbound request (GET, POST, PUT, DELETE, etc.).",
            "direction": "inbound",
            "type": "method"
        },
    "outbound.method":
        {
            "name": "Outbound HTTP Method",
            "description": "The HTTP method of the outbound request (GET, POST, PUT, DELETE, etc.).",
            "direction": "outbound",
            "type": "method"
        }
}

# LSP metadata for string literals
LSP_STRING_LITERAL_INFO = {"name": "String Literal", "description": "A quoted string value used in assignments and comparisons."}

# LSP metadata for plugin control directives
LSP_PLUGIN_CONTROL_INFO = {
    "plugin.cntl":
        {
            "name": "Plugin Control Prefix",
            "description": "Configure plugin control settings. Use with TIMEZONE or INBOUND_IP_SOURCE.",
            "qualifiers":
                {
                    "TIMEZONE":
                        {
                            "name": "Timezone Control",
                            "description": "Set timezone handling for the plugin. Valid values: GMT, LOCAL.",
                            "valid_values": ["GMT", "LOCAL"]
                        },
                    "INBOUND_IP_SOURCE":
                        {
                            "name": "Inbound IP Source Control",
                            "description": "Configure source of inbound IP address. Valid values: PEER, PROXY.",
                            "valid_values": ["PEER", "PROXY"]
                        }
                }
        }
}

# Comprehensive LSP documentation for HRW4U functions
LSP_FUNCTION_DOCUMENTATION = {
    # Statement Functions - used as statements in code blocks
    "counter":
        {
            "name": "Counter Function",
            "category": "Statistics",
            "description": "Increment an internal ATS counter statistic by 1.",
            "syntax": "counter(\"counter_name\")",
            "parameters":
                [{
                    "name": "counter_name",
                    "type": "string",
                    "description": "Name of the counter to increment. Must be quoted."
                }],
            "examples": ["counter(\"plugin.header_rewrite.teapots\");", "counter(\"custom_metric\");"],
            "maps_to": "counter",
            "usage_context": "Used for tracking custom metrics and statistics in ATS."
        },
    "run-plugin":
        {
            "name": "Run Plugin Function",
            "category": "Plugin Execution",
            "description": "Execute an external remap plugin with specified arguments.",
            "syntax": "run-plugin(\"plugin.so\", \"arg1\", \"arg2\", ...)",
            "parameters":
                [
                    {
                        "name": "plugin_file",
                        "type": "string",
                        "description": "Path to the plugin shared library file (.so)"
                    }, {
                        "name": "arguments",
                        "type": "string...",
                        "description": "Variable number of string arguments to pass to the plugin"
                    }
                ],
            "examples":
                [
                    "run-plugin(\"regex_remap.so\", \"in:^/foo/(.*)$\", \"out:/bar/$1\");",
                    "run-plugin(\"rate_limit.so\", \"--limit=300\", \"--error=429\");"
                ],
            "maps_to": "run-plugin",
            "usage_context": "Allows integration with external ATS plugins during request processing."
        },
    "set-body-from":
        {
            "name": "Set Body From URL",
            "category": "Response Manipulation",
            "description": "Set the response body content by fetching from a specified URL.",
            "syntax": "set-body-from(\"url\")",
            "parameters":
                [{
                    "name": "url",
                    "type": "string",
                    "description": "URL to fetch content from. Supports variable expansion."
                }],
            "examples": ["set-body-from(\"http://errors.example.com/500?rid={id.REQUEST}\");"],
            "maps_to": "set-body-from",
            "usage_context": "Used for custom error pages or dynamic content generation."
        },
    "set-config":
        {
            "name": "Set Configuration Variable",
            "category": "Configuration",
            "description": "Set an ATS configuration variable to a specific value for this transaction.",
            "syntax": "set-config(\"config_name\", value)",
            "parameters":
                [
                    {
                        "name": "config_name",
                        "type": "string",
                        "description": "Name of the ATS configuration variable to modify"
                    }, {
                        "name": "value",
                        "type": "string|number",
                        "description": "Value to assign to the configuration variable"
                    }
                ],
            "examples":
                [
                    "set-config(\"proxy.config.http.allow_multi_range\", 1);",
                    "set-config(\"proxy.config.http.cache.required_headers\", 0);"
                ],
            "maps_to": "set-config",
            "usage_context": "Allows per-transaction configuration overrides for fine-grained control."
        },
    "set-plugin-cntl":
        {
            "name": "Set Plugin Control",
            "category": "Plugin Configuration",
            "description": "Configure plugin control settings that affect how ATS handles requests.",
            "syntax": "set-plugin-cntl(field, value)",
            "parameters":
                [
                    {
                        "name": "field",
                        "type": "enum",
                        "description": "Plugin control field to set. Valid values: TIMEZONE, INBOUND_IP_SOURCE"
                    }, {
                        "name": "value",
                        "type": "enum",
                        "description": "Value for the field. TIMEZONE: GMT|LOCAL, INBOUND_IP_SOURCE: PEER|PROXY"
                    }
                ],
            "examples": ["set-plugin-cntl(TIMEZONE, \"GMT\");", "set-plugin-cntl(INBOUND_IP_SOURCE, \"PEER\");"],
            "maps_to": "set-plugin-cntl",
            "usage_context": "Controls plugin behavior for timezone handling and IP source detection."
        },
    "set-redirect":
        {
            "name": "Set Redirect Response",
            "category": "Response Manipulation",
            "description": "Send an HTTP redirect response to the client with specified status code and URL.",
            "syntax": "set-redirect(status_code, \"url\")",
            "parameters":
                [
                    {
                        "name": "status_code",
                        "type": "number",
                        "description": "HTTP redirect status code (300-399, typically 301, 302, 307, 308)"
                    }, {
                        "name": "url",
                        "type": "string",
                        "description": "Target URL for redirect. Supports variable expansion."
                    }
                ],
            "examples":
                [
                    "set-redirect(302, \"https://new.example.com{inbound.url.path}?{inbound.url.query}\");",
                    "set-redirect(301, \"https://secure.example.com{inbound.url.path}\");"
                ],
            "maps_to": "set-redirect",
            "usage_context": "Used for URL redirection, site migrations, and traffic routing."
        },
    "set-debug":
        {
            "name": "Enable Debug Mode",
            "category": "Debugging",
            "description": "Enable ATS transaction debugging for this specific request.",
            "syntax": "set-debug()",
            "parameters": [],
            "examples": ["if (inbound.req.X-Debug == \"supersekret\") { set-debug(); }"],
            "maps_to": "set-debug",
            "usage_context": "Enables detailed ATS logging for troubleshooting specific requests."
        },
    "skip-remap":
        {
            "name": "Skip Remap Processing",
            "category": "Request Processing",
            "description": "Skip the remap processing phase, effectively enabling open proxy behavior.",
            "syntax": "skip-remap(flag)",
            "parameters": [{
                "name": "flag",
                "type": "boolean",
                "description": "true to skip remap, false to process normally"
            }],
            "examples": ["skip-remap(true);", "if (inbound.req.X-Bypass == \"1\") { skip-remap(true); }"],
            "maps_to": "skip-remap",
            "usage_context": "Used for bypassing remap rules when specific conditions are met."
        },
    "no-op":
        {
            "name": "No Operation",
            "category": "Flow Control",
            "description": "Explicit no-operation statement that does nothing.",
            "syntax": "no-op()",
            "parameters": [],
            "examples": ["no-op();", "} else { no-op(); }"],
            "maps_to": "no-op",
            "usage_context": "Used as a placeholder or to explicitly document intentional inaction."
        },
    "remove_query":
        {
            "name": "Remove Query Parameters",
            "category": "URL Manipulation",
            "description": "Remove specific query parameters from the request URL.",
            "syntax": "remove_query(\"param1,param2,...\")",
            "parameters":
                [
                    {
                        "name": "parameters",
                        "type": "string",
                        "description": "Comma-separated list of query parameter names to remove"
                    }
                ],
            "examples": ["remove_query(\"utm_source,utm_medium\");", "remove_query(\"debug,trace\");"],
            "maps_to": "rm-destination QUERY",
            "usage_context": "Clean URLs by removing tracking or debug parameters."
        },
    "keep_query":
        {
            "name": "Keep Query Parameters",
            "category": "URL Manipulation",
            "description": "Keep only specified query parameters, removing all others.",
            "syntax": "keep_query(\"param1,param2,...\")",
            "parameters":
                [{
                    "name": "parameters",
                    "type": "string",
                    "description": "Comma-separated list of query parameter names to keep"
                }],
            "examples": ["keep_query(\"id,version\");", "keep_query(\"search,page\");"],
            "maps_to": "rm-destination QUERY [I]",
            "usage_context": "Whitelist approach to query parameter filtering."
        },

    # Condition Functions - used in conditional expressions
    "access":
        {
            "name": "File Access Check",
            "category": "File System",
            "description": "Check if a file exists and is accessible by the ATS process.",
            "syntax": "access(\"/path/to/file\")",
            "parameters": [{
                "name": "file_path",
                "type": "string",
                "description": "Absolute path to the file to check"
            }],
            "examples": ["if (access(\"/etc/maintenance.flag\")) { ... }", "if (!access(\"/path/to/healthcheck.txt\")) { ... }"],
            "maps_to": "ACCESS",
            "usage_context": "Used for maintenance modes, feature flags, and health checks."
        },
    "cache":
        {
            "name": "Cache Status Check",
            "category": "Cache Management",
            "description": "Get the cache lookup result status for the current request.",
            "syntax": "cache()",
            "parameters": [],
            "examples": ["if (cache() == \"hit-fresh\") { ... }", "if (cache() in [\"miss\", \"skipped\"]) { ... }"],
            "maps_to": "CACHE",
            "usage_context": "Used to make decisions based on cache hit/miss status.",
            "return_values": ["hit-fresh", "hit-stale", "miss", "skipped", "updated", "refresh"]
        },
    "cidr":
        {
            "name": "CIDR Network Match",
            "category": "Network",
            "description": "Match client IP address against CIDR network blocks.",
            "syntax": "cidr(ipv4_bits, ipv6_bits)",
            "parameters":
                [
                    {
                        "name": "ipv4_bits",
                        "type": "number",
                        "description": "Number of bits for IPv4 network mask (1-32)"
                    }, {
                        "name": "ipv6_bits",
                        "type": "number",
                        "description": "Number of bits for IPv6 network mask (1-128)"
                    }
                ],
            "examples": ["if (cidr(16, 48) == \"10.0.0.0\") { ... }", "if (cidr(24, 64) == \"192.168.1.0\") { ... }"],
            "maps_to": "CIDR",
            "usage_context": "Used for IP-based access control and geographic routing."
        },
    "internal":
        {
            "name": "Internal Transaction Check",
            "category": "Transaction Analysis",
            "description": "Check if the current transaction was generated internally by ATS.",
            "syntax": "internal()",
            "parameters": [],
            "examples": ["if (internal()) { ... }", "if (!internal() && access(\"/etc/external.flag\")) { ... }"],
            "maps_to": "INTERNAL-TRANSACTION",
            "usage_context": "Differentiate between external client requests and internal ATS operations."
        },
    "random":
        {
            "name": "Random Number Generator",
            "category": "Randomization",
            "description": "Generate a random number between 0 and the specified maximum (exclusive).",
            "syntax": "random(max_value)",
            "parameters":
                [
                    {
                        "name": "max_value",
                        "type": "number",
                        "description": "Maximum value for random number generation (exclusive upper bound)"
                    }
                ],
            "examples": [
                "if (random(100) < 10) { ... }  // 10% probability", "if (random(1000) > 500) { ... }  // 50% probability"
            ],
            "maps_to": "RANDOM",
            "usage_context": "Used for A/B testing, sampling, and probabilistic routing."
        },
    "ssn-txn-count":
        {
            "name": "Server Session Transaction Count",
            "category": "Connection Analysis",
            "description": "Get the number of transactions processed on the current server connection.",
            "syntax": "ssn-txn-count()",
            "parameters": [],
            "examples": ["if (ssn-txn-count() > 10) { ... }", "if (ssn-txn-count() == 1) { ... }  // First request on connection"],
            "maps_to": "SSN-TXN-COUNT",
            "usage_context": "Used for connection reuse analysis and server-side load balancing."
        },
    "txn-count":
        {
            "name": "Client Transaction Count",
            "category": "Connection Analysis",
            "description": "Get the number of transactions processed on the current client connection.",
            "syntax": "txn-count()",
            "parameters": [],
            "examples": ["if (txn-count() > 5) { ... }", "if (txn-count() == 1) { ... }  // First request from client"],
            "maps_to": "TXN-COUNT",
            "usage_context": "Used for client behavior analysis and connection management."
        }
}

# Special keywords and control flow documentation
LSP_KEYWORD_DOCUMENTATION = {
    "break":
        {
            "name": "Break Statement",
            "category": "Flow Control",
            "description": "Exit the current section early, equivalent to no-op [L] in header_rewrite.",
            "syntax": "break;",
            "examples": ["if (condition) { inbound.resp.Cache-Control = \"max-age=3600\"; break; }"],
            "maps_to": "no-op [L]",
            "usage_context": "Used to terminate processing in the current section when a condition is met."
        }
}
