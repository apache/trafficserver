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

import requests
import logging

#import helpers
import tsqa.test_cases
import tsqa.utils
import tsqa.endpoint
import os


log = logging.getLogger(__name__)

# Since at this moment tha plan is to treat all query, headers and cookie related
# plugin parameters a similar way - include | exclude | remove-all | sort decided to create
# a 'meta' bench and then use it to create / adjust the corresponding query, headers and cookie
# related test benches and use to to validate the plugin behavoior. TBD how well that works.
meta_bench = [
            # Testing empty parametes and defaults.
            { "args": "",
              "uri": [('c','1'),('a','1'),('b','2'),('x','1'),('k','1'),('u','1'),('y','1')],
              "key": [('c','1'),('a','1'),('b','2'),('x','1'),('k','1'),('u','1'),('y','1')] },
            { "args": [('include', [])],
              "uri": [('c','1'),('a','1'),('b','2'),('x','1'),('k','1'),('u','1'),('y','1')],
              "key": [('c','1'),('a','1'),('b','2'),('x','1'),('k','1'),('u','1'),('y','1')] },
            { "args": [('exclude',[])],
              "uri": [('c','1'),('a','1'),('b','2'),('x','1'),('k','1'),('u','1'),('y','1')],
              "key": [('c','1'),('a','1'),('b','2'),('x','1'),('k','1'),('u','1'),('y','1')] },
            { "args": [('exclude', []), ('include', [])],
              "uri": [('c','1'),('a','1'),('b','2'),('x','1'),('k','1'),('u','1'),('y','1')],
              "key": [('c','1'),('a','1'),('b','2'),('x','1'),('k','1'),('u','1'),('y','1')] },
            { "args": [('remove-all', [])],
              "uri": [('c','1'),('a','1'),('b','2'),('x','1'),('k','1'),('u','1'),('y','1')],
              "key": [('c','1'),('a','1'),('b','2'),('x','1'),('k','1'),('u','1'),('y','1')] },

            # Testing the removal of query parameters from the cache key.
            { "args": [('remove-all', [])],
              "uri": [('c','1'),('a','1'),('b','2'),('x','1'),('k','1'),('u','1'),('y','1')],
              "key": [('c','1'),('a','1'),('b','2'),('x','1'),('k','1'),('u','1'),('y','1')] },
            { "args": [('remove-all', ['false'])],
              "uri": [('c','1'),('a','1'),('b','2'),('x','1'),('k','1'),('u','1'),('y','1')],
              "key": [('c','1'),('a','1'),('b','2'),('x','1'),('k','1'),('u','1'),('y','1')] },
            { "args": [('remove-all', ['true'])],
              "uri": [('c','1'),('a','1'),('b','2'),('x','1'),('k','1'),('u','1'),('y','1')],
              "key": [] },

            # Testing the sorting of the query parameters in the cache key.
            { "args": [('sort', [])],
              "uri": [('c','1'),('a','1'),('b','2'),('x','1'),('k','1'),('u','1'),('y','1')],
              "key": [('c','1'),('a','1'),('b','2'),('x','1'),('k','1'),('u','1'),('y','1')] },
            { "args": [('sort', ['false'])],
              "uri": [('c','1'),('a','1'),('b','2'),('x','1'),('k','1'),('u','1'),('y','1')],
              "key": [('c','1'),('a','1'),('b','2'),('x','1'),('k','1'),('u','1'),('y','1')] },
            { "args": [('sort', ['true'])],
              "uri": [('c','1'),('a','1'),('b','2'),('x','1'),('k','1'),('u','1'),('y','1')],
              "key": [('a','1'),('b','2'),('c','1'),('k','1'),('u','1'),('x','1'),('y','1')] },
            { "args": [('sort', []), ('remove-all', [])],
              "uri": [('c','1'),('a','1'),('b','2'),('x','1'),('k','1'),('u','1'),('y','1')],
              "key": [('c','1'),('a','1'),('b','2'),('x','1'),('k','1'),('u','1'),('y','1')] },

            # Testing the exclusion of query parameters from the cache key.
            { "args": [('exclude', ['x','y','z'])],
              "uri": [('c','1'),('a','1'),('b','2'),('x','1'),('k','1'),('u','1'),('y','1')],
              "key": [('c','1'),('a','1'),('b','2'),('k','1'),('u','1')] },
            { "args": [('exclude', ['x','y','z']), ('include', [])],
              "uri": [('c','1'),('a','1'),('b','2'),('x','1'),('k','1'),('u','1'),('y','1')],
              "key": [('c','1'),('a','1'),('b','2'),('k','1'),('u','1')] },
            { "args": [('exclude', ['x','y','z']), ('include', []), ('sort', ['true'])],
              "uri": [('c','1'),('a','1'),('b','2'),('x','1'),('k','1'),('u','1'),('y','1')],
              "key": [('a','1'),('b','2'),('c','1'),('k','1'),('u','1')] },

            # Testing the inclusion of query parameters in the cache key.
            { "args": [('include', ['x','y','b','c'])],
              "uri": [('c','1'),('a','1'),('b','2'),('x','1'),('k','1'),('u','1'),('y','1')],
              "key": [('c','1'),('b','2'),('x','1'),('y','1')] },
            { "args": [('include', ['x','y','b','c', 'g'])],
              "uri": [('c','1'),('a','1'),('b','2'),('x','1'),('k','1'),('u','1'),('y','1')],
              "key": [('c','1'),('b','2'),('x','1'),('y','1')] },
            { "args": [('include', ['x','y','b','c']), ('exclude', [])],
              "uri": [('c','1'),('a','1'),('b','2'),('x','1'),('k','1'),('u','1'),('y','1')],
              "key": [('c','1'),('b','2'),('x','1'),('y','1')] },
            { "args": [('include', ['x','y','b','c']), ('sort', ['true'])],
              "uri": [('c','1'),('a','1'),('b','2'),('x','1'),('k','1'),('u','1'),('y','1')],
              "key": [('b','2'),('c','1'),('x','1'),('y','1')] },

            # Testing various useful cases (combinations) to include/exclude/sort query parameters in the cache key.
            { "args": [('exclude', ['x','y','z']), ('include', ['x','y','b','c'])],
              "uri": [('c','1'),('a','1'),('b','2'),('x','1'),('k','1'),('u','1'),('y','1')],
              "key": [('c','1'),('b','2')] },
            { "args": [('exclude', ['x','y','z']), ('include', [])],
              "uri": [('c','1'),('a','1'),('b','2'),('x','1'),('k','1'),('u','1'),('y','1')],
              "key": [('c','1'),('a','1'),('b','2'),('k','1'),('u','1')] },
            { "args": [('exclude', ['x','y','z']), ('include', []), ('sort', ['true'])],
              "uri": [('c','1'),('a','1'),('b','2'),('x','1'),('k','1'),('u','1'),('y','1')],
              "key": [('a','1'),('b','2'),('c','1'),('k','1'),('u','1')] },
            { "args": [('exclude', ['x','y','z']), ('include', ['x','y','b','c']), ('sort', ['true']), ('remove-all', ['true'])],
              "uri": [('c','1'),('a','1'),('b','2'),('x','1'),('k','1'),('u','1'),('y','1')],
              "key": [] },
            { "args": [('exclude', ['x']), ('exclude', ['y']), ('exclude', ['z']), ('include', ['y','c']), ('include', ['x','b'])],
              "uri": [('c','1'),('a','1'),('b','2'),('x','1'),('k','1'),('u','1'),('y','1')],
              "key": [('c','1'),('b','2')] },

            # Testing regex include-match.
            { "args": [('include-match', ['(a|b|c)']),],
              "uri": [('c','1'),('a','1'),('b','2'),('x','1'),('k','1'),('u','1'),('y','1')],
              "key": [('c','1'),('a','1'),('b','2'),] },
            # Testing multiple regex include-match with pattern that don't match ('k' and 'u').
            { "args": [('include-match', ['(a|b|c)']), ('include-match', ['(x|y|z)'])],
              "uri": [('c','1'),('a','1'),('b','2'),('x','1'),('k','1'),('u','1'),('y','1')],
              "key": [('c','1'),('a','1'),('b','2'),('x','1'),('y','1')] },
            # Testing regex exclude match.
            { "args": [('exclude-match', ['(a|b|c)']),],
              "uri": [('c','1'),('a','1'),('b','2'),('x','1'),('k','1'),('u','1'),('y','1')],
              "key": [('x','1'),('k','1'),('u','1'),('y','1')] },
            # Testing multiple regex exclude-match with pattern that don't match ('k' and 'u').
            { "args": [('exclude-match', ['(a|b|c)']), ('exclude-match', ['(x|y|z)'])],
              "uri": [('c','1'),('a','1'),('b','2'),('x','1'),('k','1'),('u','1'),('y','1')],
              "key": [('k','1'),('u','1')] },
            # Testing mixing exclude and include match
            { "args": [('include-match', ['(a|b|c|x)']), ('exclude-match', ['(x|y|z)'])],
              "uri": [('c','1'),('a','1'),('b','2'),('x','1'),('k','1'),('u','1'),('y','1')],
              "key": [('c','1'),('a','1'),('b','2')] },
            # Testing mixing exclude and include match
            { "args": [('exclude-match', ['x']), ('exclude-match', ['y']), ('exclude-match', ['z']), ('include-match', ['(y|c)']), ('include-match', ['(x|b)'])],
              "uri": [('c','1'),('a','1'),('b','2'),('x','1'),('k','1'),('u','1'),('y','1')],
              "key": [('c','1'),('b','2')] },
            # Testing mixing `--include-params`, `--exclude-params`, `--include-match-param` and `--exclude-match-param`
            { "args": [('exclude', ['x']), ('exclude-match', ['y']), ('exclude-match', ['z']), ('include', ['y','c']), ('include-match', ['(x|b)'])],
              "uri": [('c','1'),('a','1'),('b','2'),('x','1'),('k','1'),('u','1'),('y','1')],
              "key": [('c','1'),('b','2')] },
        ]

# Query related bench - meta_bench is used to populate it.
query_bench = []

# Headers related bench - meta_bench is used to populate it.
headers_bench = []

# Cookies related bench - meta_bench is used to populate it.
cookies_bench = []

# Prefix related tests. Doesn't use the meta_bench.
prefix_bench = [
            # Testing not adding any custom prefix
            { "args": "",
              "uri": "{0}:{1}/path/to/object?a=1&b=2&c=3",
              "headers": [],
              "cookies": [],
              "key": "/{0}/{1}/path/to/object?a=1&b=2&c=3"
            },
            # Testing using the option but with no value
            { "args": "@pparam=--static-prefix=",
              "uri": "{0}:{1}/path/to/object?a=1&b=2&c=3",
              "headers": [],
              "cookies": [],
              "key": "/{0}/{1}/path/to/object?a=1&b=2&c=3"
            },
            # Testing adding a static prefix to the cache key
            { "args": "@pparam=--static-prefix=static_prefix",
              "uri": "{0}:{1}/path/to/object?a=1&b=2&c=3",
              "headers": [],
              "cookies": [],
              "key": "/static_prefix/path/to/object?a=1&b=2&c=3"
            },
            # Testing using the option but with no value
            { "args": "@pparam=--capture-prefix=",
              "uri": "{0}:{1}/path/to/object?a=1&b=2&c=3",
              "headers": [],
              "cookies": [],
              "key": "/{0}/{1}/path/to/object?a=1&b=2&c=3"
            },
            # Testing adding a capture prefix to the cache key
            { "args": "@pparam=--capture-prefix=(test_prefix).*:([^\s\/$]*)",
              "uri": "{0}:{1}/path/to/object?a=1&b=2&c=3",
              "headers": [],
              "cookies": [],
              "key": "/test_prefix/{1}/path/to/object?a=1&b=2&c=3"
            },
            # Testing adding a capture prefix with replacement string defined
            { "args": "@pparam=--capture-prefix=/(test_prefix).*:([^\s\/]*)/$1_$2/",
              "uri": "{0}:{1}/path/to/object?a=1&b=2&c=3",
              "headers": [],
              "cookies": [],
              "key": "/test_prefix_{1}/path/to/object?a=1&b=2&c=3"
            },
            # Testing adding a capture prefix from URI to the cache key
            { "args": "@pparam=--capture-prefix-uri=(test_prefix).*:.*(object).*$",
              "uri": "{0}:{1}/path/to/object?a=1&b=2&c=3",
              "headers": [],
              "cookies": [],
              "key": "/test_prefix/object/path/to/object?a=1&b=2&c=3"
            },
            # Testing adding a capture prefix from with replacement string defined
            { "args": "@pparam=--capture-prefix-uri=/(test_prefix).*:.*(object).*$/$1_$2/",
              "uri": "{0}:{1}/path/to/object?a=1&b=2&c=3",
              "headers": [],
              "cookies": [],
              "key": "/test_prefix_object/path/to/object?a=1&b=2&c=3"
            },
            # Testing adding both static and capture prefix to the cache key
            { "args": "@pparam=--static-prefix=static_prefix @pparam=--capture-prefix=(test_prefix).*",
              "uri": "{0}:{1}/path/to/object?a=1&b=2&c=3",
              "headers": [],
              "cookies": [],
              "key": "/static_prefix/test_prefix/path/to/object?a=1&b=2&c=3"
            },
            # Testing adding static and capture prefix and capture prefix from URI to the cache key
            { "args": "@pparam=--static-prefix=static_prefix @pparam=--capture-prefix=(test_prefix).* @pparam=--capture-prefix-uri=(object).*",
              "uri": "{0}:{1}/path/to/object?a=1&b=2&c=3",
              "headers": [],
              "cookies": [],
              "key": "/static_prefix/test_prefix/object/path/to/object?a=1&b=2&c=3"
            },
        ]
path_bench = [
            # Testing adding default path to the cache key
            { "args": "",
              "uri": "{0}:{1}/path/to/object?a=1&b=2&c=3",
              "headers": [],
              "cookies": [],
              "key": "/{0}/{1}/path/to/object?a=1&b=2&c=3"
            },
            # Testing adding a path capture to the cache key
            { "args": "@pparam=--capture-path=.*(object).*",
              "uri": "{0}:{1}/path/to/object?a=1&b=2&c=3",
              "headers": [],
              "cookies": [],
              "key": "/{0}/{1}/object?a=1&b=2&c=3"
            },
            # Testing adding a path capture/replacement to the cache key
            { "args": "@pparam=--capture-path=/.*(object).*/const_path_$1/",
              "uri": "{0}:{1}/path/to/object?a=1&b=2&c=3",
              "headers": [],
              "cookies": [],
              "key": "/{0}/{1}/const_path_object?a=1&b=2&c=3"
            },
            # Testing adding an URI capture to the cache key
            { "args": "@pparam=--capture-path-uri=(test_path).*(object).*",
              "uri": "{0}:{1}/path/to/object?a=1&b=2&c=3",
              "headers": [],
              "cookies": [],
              "key": "/{0}/{1}/test_path/object?a=1&b=2&c=3"
            },
            # Testing adding an URI capture/replacement to the cache key
            { "args": "@pparam=--capture-path-uri=/(test_path).*(object).*/$1_$2/",
              "uri": "{0}:{1}/path/to/object?a=1&b=2&c=3",
              "headers": [],
              "cookies": [],
              "key": "/{0}/{1}/test_path_object?a=1&b=2&c=3"
            },
            # Testing adding an URI and path capture/replacement together to the cache key
            { "args": "@pparam=--capture-path=/.*(object).*/const_path_$1/ @pparam=--capture-path-uri=/(test_path).*(object).*/$1_$2/",
              "uri": "{0}:{1}/path/to/object?a=1&b=2&c=3",
              "headers": [],
              "cookies": [],
              "key": "/{0}/{1}/test_path_object/const_path_object?a=1&b=2&c=3"
            },
        ]


# User-Agent header capture related tests. Doesn't use the meta_bench.
ua_captures_bench = [
            # Testing single match without grouping.
            { "args": "@pparam=--ua-capture=Mozilla\/[^\s]*",
              "uri": "{0}:{1}/path/to/object?a=1&b=2&c=3",
              "headers": [("User-Agent", "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_9_3) AppleWebKit/537.75.14 (KHTML, like Gecko) Version/7.0.3 Safari/7046A194A")],
              "cookies": [],
              "key": "/{0}/{1}/Mozilla/5.0/path/to/object?a=1&b=2&c=3"
            },
            # Testing single match with grouping.
            { "args": "@pparam=--ua-capture=(Mozilla\/[^\s]*)",
              "uri": "{0}:{1}/path/to/object?a=1&b=2&c=3",
              "headers": [("User-Agent", "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_9_3) AppleWebKit/537.75.14 (KHTML, like Gecko) Version/7.0.3 Safari/7046A194A")],
              "cookies": [],
              "key": "/{0}/{1}/Mozilla/5.0/path/to/object?a=1&b=2&c=3"
            },
            # Testing multiple capturing group match.
            { "args": "@pparam=--ua-capture=(Mozilla\/[^\s]*).*(AppleWebKit\/[^\s]*)",
              "uri": "{0}:{1}/path/to/object?a=1&b=2&c=3",
              "headers": [("User-Agent", "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_9_3) AppleWebKit/537.75.14 (KHTML, like Gecko) Version/7.0.3 Safari/7046A194A")],
              "cookies": [],
              "key": "/{0}/{1}/Mozilla/5.0/AppleWebKit/537.75.14/path/to/object?a=1&b=2&c=3"
            },
            # Testing multiple capturing group match with empty replacement string.
            { "args": "@pparam=--ua-capture=/(Mozilla\/[^\s]*).*(AppleWebKit\/[^\s]*)//",
              "uri": "{0}:{1}/path/to/object?a=1&b=2&c=3",
              "headers": [("User-Agent", "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_9_3) AppleWebKit/537.75.14 (KHTML, like Gecko) Version/7.0.3 Safari/7046A194A")],
              "cookies": [],
              "key": "/{0}/{1}/Mozilla/5.0/AppleWebKit/537.75.14/path/to/object?a=1&b=2&c=3"
            },
            # Testing multiple capturing group match with the replacement.
            { "args": "@pparam=--ua-capture=/(Mozilla\/[^\s]*).*(AppleWebKit\/[^\s]*)/$1_$2/",
              "uri": "{0}:{1}/path/to/object?a=1&b=2&c=3",
              "headers": [("User-Agent", "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_9_3) AppleWebKit/537.75.14 (KHTML, like Gecko) Version/7.0.3 Safari/7046A194A")],
              "cookies": [],
              "key": "/{0}/{1}/Mozilla/5.0_AppleWebKit/537.75.14/path/to/object?a=1&b=2&c=3"
            },
            # Testing multiple capturing group match with $0 (zero group) in the replacement.
            { "args": "@pparam=--ua-capture=/(Mozilla\/[^\s]*).*(AppleWebKit\/[^\s]*)/$0/",
              "uri": "{0}:{1}/path/to/object?a=1&b=2&c=3",
              "headers": [("User-Agent", "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_9_3) AppleWebKit/537.75.14 (KHTML, like Gecko) Version/7.0.3 Safari/7046A194A")],
              "cookies": [],
              "key": "/{0}/{1}/Mozilla/5.0%20(Macintosh;%20Intel%20Mac%20OS%20X%2010_9_3)%20AppleWebKit/537.75.14/path/to/object?a=1&b=2&c=3"
            },
            # Testing an extra invalid variable in the replacement, the whole capture will be ignored (TODO verify the error message in the log).
            { "args": "@pparam=--ua-capture=/(Mozilla\/[^\s]*).*(AppleWebKit\/[^\s]*)/$1_$2_$3/",
              "uri": "{0}:{1}/path/to/object?a=1&b=2&c=3",
              "headers": [("User-Agent", "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_9_3) AppleWebKit/537.75.14 (KHTML, like Gecko) Version/7.0.3 Safari/7046A194A")],
              "cookies": [],
              "key": "/{0}/{1}/path/to/object?a=1&b=2&c=3"
            },
        ]

ua_classifier_bench = [
            # Testing ua-blacklist.
            { "args": "@pparam=--ua-blacklist=class1:class1_blacklist.config",
              "uri": "{0}:{1}/path/to/object?a=1&b=2&c=3",
              "headers": [("User-Agent", "Bozilla/5.0 (Macintosh; Intel Mac OS X 10_9_3) AppleWebKit/537.75.14 (KHTML, like Gecko) Version/7.0.3 Safari/7046A194A")],
              "cookies": [],
              "key": "/{0}/{1}/class1/path/to/object?a=1&b=2&c=3",
              "files": [("class1_blacklist.config", "^Mozilla.*\n^AdSheet.*\n^iTube.*\n^TuneIn.*\n^iHeartRadio.*\n^Ruby.*\n^python.*\n^Twitter.*\n^Facebo.*\n")],
            },
            # Testing ua-whitelist.
            { "args": "@pparam=--ua-whitelist=class1:class1_blacklist.config",
              "uri": "{0}:{1}/path/to/object?a=1&b=2&c=3",
              "headers": [("User-Agent", "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_9_3) AppleWebKit/537.75.14 (KHTML, like Gecko) Version/7.0.3 Safari/7046A194A")],
              "cookies": [],
              "key": "/{0}/{1}/class1/path/to/object?a=1&b=2&c=3",
              "files": [("class1_blacklist.config", "^Mozilla.*\n^AdSheet.*\n^iTube.*\n^TuneIn.*\n^iHeartRadio.*\n^Ruby.*\n^python.*\n^Twitter.*\n^Facebo.*\n")],
            },
            # Testing ua-whitelist and ua-blacklist together, whitelist specified before blacklist.
            { "args": "@pparam=--ua-whitelist=class1:class1_whitelist.config @pparam=--ua-blacklist=class2:class2_blacklist.config",
              "uri": "{0}:{1}/path/to/object?a=1&b=2&c=3",
              "headers": [("User-Agent", "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_9_3) AppleWebKit/537.75.14 (KHTML, like Gecko) Version/7.0.3 Safari/7046A194A")],
              "cookies": [],
              "key": "/{0}/{1}/class1/path/to/object?a=1&b=2&c=3",
              "files": [("class1_whitelist.config", "^Mozilla.*\n^AdSheet.*\n^iTube.*\n^TuneIn.*\n"),
                        ("class2_blacklist.config", "^iHeartRadio.*\n^Ruby.*\n^python.*\n^Twitter.*\n^Facebo.*\n")],
            },
            # Testing ua-whitelist and ua-blacklist together, blacklist specified before whitelist.
            { "args": "@pparam=--ua-blacklist=class2:class2_blacklist.config @pparam=--ua-whitelist=class1:class1_whitelist.config",
              "uri": "{0}:{1}/path/to/object?a=1&b=2&c=3",
              "headers": [("User-Agent", "Bozilla/5.0 (Macintosh; Intel Mac OS X 10_9_3) AppleWebKit/537.75.14 (KHTML, like Gecko) Version/7.0.3 Safari/7046A194A")],
              "cookies": [],
              "key": "/{0}/{1}/class2/path/to/object?a=1&b=2&c=3",
              "files": [("class1_whitelist.config", "^Mozilla.*\n^AdSheet.*\n^iTube.*\n^TuneIn.*\n"),
                        ("class2_blacklist.config", "^iHeartRadio.*\n^Ruby.*\n^python.*\n^Twitter.*\n^Facebo.*\n")],
            },
        ]

def prepare_query_bench(bench):
    new_bench = []
    for test in bench:
        args = ''

        for arg in test['args']:
            args += '@pparam=--{0}-params='.format(arg[0])
            args += ','.join(map(str,arg[1]))
            args += ' '

        uri = '{0}:{1}/?'
        kvp_list = []
        for (k,v) in test['uri']:
            kvp_list.append('{0}={1}'.format(k,v))
        uri += '&'.join(map(str, kvp_list))

        key = '/{0}/{1}'
        if len(test['key']) != 0:
            key += '?'
        kvp_list = []
        for (k,v) in test['key']:
            kvp_list.append('{0}={1}'.format(k,v))
        key += '&'.join(map(str, kvp_list))

        headers = []

        new_test = { "args": args.strip(), "uri": uri.strip(), "headers": headers,  "cookies": [], "key": key.strip() }
        new_bench.append(new_test)

    return new_bench


def prepare_headers_bench(bench):
    new_bench = []
    for test in bench:
        args = ''
        ignore_test = False

        include = []
        exclude = []

        for arg in test['args']:
            # 'exclude', 'exclude-match', 'sort', 'remove-all' don't make sense for headers as far cachekey is concerned.
            # headers always sorted and never included by default.
            if arg[0] == 'exclude' or arg[0] == 'sort' or arg[0] == 'remove-all' or arg[0] == 'include-match' or arg[0] == 'exclude-match':
                ignore_test=True
                break

            if arg[0] == 'include' and len(arg[1]) != 0:
                include.append(arg[1])

            args += '@pparam=--{0}-headers='.format(arg[0])
            args += ','.join(map(str,arg[1]))
            args += ' '

        if ignore_test:
            continue

        uri = '{0}:{1}/'

        headers = test['uri']

        key = '/{0}/{1}'

        # if there nothing to include and nothing to exclude don't add headers to the cache key.
        if len(include) != 0 or len(exclude) != 0:
            if len(test['key']) != 0:
                key += '/'
            kvp_list = []
            for (k,v) in test['key']:
                kvp_list.append('{0}:{1}'.format(k,v))
                kvp_list.sort()
            key += '/'.join(map(str, kvp_list))

        new_test = { "args": args.strip(), "uri": uri.strip(), "headers": headers, "cookies": [], "key": key.strip() }
        new_bench.append(new_test)

    return new_bench

def prepare_cookies_bench(bench):
    new_bench = []
    for test in bench:
        args = ''
        ignore_test = False

        include = []
        exclude = []

        for arg in test['args']:
            # 'exclude', 'exclude-match', 'sort', 'remove-all' don't make sense for cookies as far cachekey is concerned.
            # headers always sorted and never included by default.
            if arg[0] == 'exclude' or arg[0] == 'sort' or arg[0] == 'remove-all' or arg[0] == 'include-match' or arg[0] == 'exclude-match':
                ignore_test=True
                break

            if arg[0] == 'include' and len(arg[1]) != 0:
                include.append(arg[1])

            args += '@pparam=--{0}-cookies='.format(arg[0])
            args += ','.join(map(str,arg[1]))
            args += ' '


        if ignore_test:
            continue

        uri = '{0}:{1}/'

        cookies = test['uri']

        key = '/{0}/{1}'
        # if there nothing to include and nothing to exclude don't add headers to the cache key.
        if len(include) != 0 or len(exclude) != 0:
            if len(test['key']) != 0:
                key += '/'
            kvp_list = []
            for (k,v) in test['key']:
                kvp_list.append('{0}={1}'.format(k,v))
            kvp_list.sort()
            key += ';'.join(map(str, kvp_list))

        new_test = { "args": args.strip(), "uri": uri.strip(), "headers": [], "cookies": cookies, "key": key.strip() }
        new_bench.append(new_test)

    return new_bench


class StaticEnvironmentCase(tsqa.test_cases.EnvironmentCase):
    '''
    Use static environment, to be able to experiment and speedup builds through ramdisk
    Use this until it is merged into master (pull-request) then fall-back to helpers.EnvironmentCase class
    '''
    @classmethod
    def getEnv(cls):
        layout = tsqa.environment.Layout('/opt/apache/trafficserver.TS-4183/')
        env = tsqa.environment.Environment()
        env.clone(layout=layout)
        return env

class TestCacheKey(tsqa.test_cases.DynamicHTTPEndpointCase, StaticEnvironmentCase):

    @classmethod
    def setUpEnv(cls, env):
        global query_bench
        global headers_bench
        global cookies_bench
        global meta_bench

        cls.configs['plugin.config'].add_line('xdebug.so')

        cls.configs['records.config']['CONFIG'].update({
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'cachekey.*',
            'proxy.config.url_remap.pristine_host_hdr': 1,
        })

        log.info("Initializing remap rules")

        def add_remap_rule(remap_prefix, remap_index, test):
            host = 'test_{0}_{1}.example.com'.format(remap_prefix, remap_index)
            port = cls.configs['records.config']['CONFIG']['proxy.config.http.server_ports']
            args = test['args']
            remap_rule = 'map http://{0}:{1} http://127.0.0.1:{2} @plugin=cachekey.so {3}'.format(host, port, cls.http_endpoint.address[1], args)
            log.info('  {0}'.format(remap_rule))
            cls.configs['remap.config'].add_line(remap_rule)

        log.info("Preparing cache key query hadnling test bench")
        query_bench = prepare_query_bench(meta_bench);

        log.info("Preparing cache key headers handling test bench")
        headers_bench = prepare_headers_bench(meta_bench)

        log.info("Preparing cache key cookies handling test bench")
        cookies_bench = prepare_cookies_bench(meta_bench)

        # Prepare query tests related remap rules.
        i = 0
        for test in query_bench:
            add_remap_rule("query", i, test)
            i+=1

        # Prepare headers tests related remap rules.
        i = 0
        for test in headers_bench:
            add_remap_rule("headers", i, test)
            i+=1

        # Prepare headers tests related remap rules.
        i = 0
        for test in cookies_bench:
            add_remap_rule("cookies", i, test)
            i+=1

        # Prepare prefix tests related remap rules.
        i = 0
        for test in prefix_bench:
            add_remap_rule("prefix", i, test)
            i+=1

        # Prepare path tests related remap rules.
        i = 0
        for test in path_bench:
            add_remap_rule("path", i, test)
            i+=1

        # Prepare ua-capture tests related remap rules.
        i = 0
        for test in ua_captures_bench:
            add_remap_rule("ua_captures", i, test)
            i+=1

        # Prepare ua-classifier tests related remap rules.
        i = 0
        for test in ua_classifier_bench:
            add_remap_rule("ua_classifier", i, test)

            # Create blacklist and white list files for User-Agent classification.
            for file in test['files']:
                filename = file[0]
                content = file[1]
                path = os.path.join(env.layout.prefix, 'etc/trafficserver', filename);
                with open(path, 'w') as fh:
                    fh.write(content)

            i+=1

        # Set up an origin server which returns OK all the time.
        def handler(request):
            return ('OK', 200, {"Cache-Control": "max-age=5 must-revalidate"})

        cls.http_endpoint.add_handler('/', handler)
        cls.http_endpoint.add_handler('/path/to/object', handler)


    def get_cachekey(self, host, port, uri, headers, cookies):
        '''
        Sends a request to the traffic server and gets the cache key used while processing the request.
        '''
        uri_req = uri.format('http://127.0.0.1', port)
        s = requests.Session()
        s.headers.update({'Host': '{0}:{1}'.format(host, port)})
        s.headers.update({'X-Debug': 'X-Cache-Key'})
        for header_name, header_value in headers:
            s.headers.update({header_name: header_value})
        for cookie_name, cookie_value in cookies:
            s.cookies.set(cookie_name, cookie_value)
        response = s.get(uri_req)
        self.assertEqual(response.status_code, 200)
        return response.headers['X-Cache-Key']

    def verify_key(self, remap_prefix, remap_index, test):
        host = 'test_{0}_{1}.example.com'.format( remap_prefix, remap_index)
        port = self.configs['records.config']['CONFIG']['proxy.config.http.server_ports']
        expected_key = test['key'].format(host, port)
        key = self.get_cachekey(host, port, test['uri'], test['headers'], test['cookies'])
        log.info("  Test {0} / {1}".format(remap_prefix, remap_index))
        log.info("    map : cachekey.so {0}".format(test['args']))
        log.info("    uri :'{0}'".format(test['uri']))
        headers = ''
        for name,value in test['headers']:
            headers += "'{0}: {1}' ".format(name, value)
        cookies = ''
        for name,value in test['cookies']:
            cookies += "'{0}: {1}' ".format(name, value)
        log.info("    headers: {0}".format(headers))
        log.info("    cookies: {0}".format(cookies))
        log.info("    expected:'{0}'".format(expected_key))
        log.info("    received:'{0}'".format(key))

        self.assertEqual(key, expected_key)

    def test_cachekey_query(self):
        '''
        Testing cache key query parameters handling.
        '''
        global query_bench

        log.info("Testing cache key query parameters handling.")
        i = 0
        for test in query_bench:
            self.verify_key('query', i, test)
            i += 1

    def test_cachekey_preffix(self):
        '''
        Tests --static-prefix, --capture-prefix, --capture-prefix-uri plugin option in the cache key.
        '''
        global prifix_bench

        log.info("Testing --static-prefix, --capture-prefix, --capture-prefix-uri plugin option in the cache key.")
        i = 0
        for test in prefix_bench:
            self.verify_key('prefix', i, test)
            i += 1

    def test_cachekey_path(self):
        '''
        Tests --path-capture, --path-capture-uri plugin option for replacing path in the cache key.
        '''
        global path_bench

        log.info("Testing --path-capture, --path-capture-uri plugin option for replacing path in the cache key.")
        i = 0
        for test in path_bench:
            self.verify_key('path', i, test)
            i += 1

    def test_cachekey_headers(self):
        '''
        Testing cache key headers handling.
        '''
        global headers_bench

        log.info("Testing cache key headers handling.")
        i = 0
        for test in headers_bench:
            self.verify_key('headers', i, test)
            i += 1

    def test_cachekey_cookies(self):
        '''
        Testing cache key cookies handling.
        '''
        global cookies_bench

        log.info("Testing cache key cookies handling.")
        i = 0
        for test in cookies_bench:
            self.verify_key('cookies', i, test)
            i += 1

    def test_cachekey_ua_capture(self):
        '''
        Testing cache key User-Agent header capture handling.
        '''
        global cookies_bench

        log.info("Testing cache key User-Agent header capture handling.")
        i = 0
        for test in ua_captures_bench:
            self.verify_key('ua_captures', i, test)
            i += 1

    def test_cachekey_ua_classifier(self):
        '''
        Testing cache key User-Agent header classifier.
        '''
        global cookies_bench

        log.info("Testing cache key User-Agent header capture handling.")
        i = 0
        for test in ua_classifier_bench:
            self.verify_key('ua_classifier', i, test)
            i += 1
