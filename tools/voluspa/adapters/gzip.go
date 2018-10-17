/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

package adapters

import (
	"bytes"
	"fmt"
	"sort"
	"strings"

	"github.com/apache/trafficserver/tools/voluspa"
)

const (
	GzipAdapterParameterCompressibleContentType = "static_origin_compressible_content_type"
	GzipAdapterParameter                        = "edge_compression"
	GzipAdapterParameterEnabled                 = "enabled"
	GzipAdapterParameterAlgorithms              = "algorithms"
	GzipAdapterParameterRemoveAcceptEncoding    = "remove_accept_encoding"
	GzipAdapterParameterFlush                   = "flush"
	GzipAdapterParameterMinimumContentLength    = "minimum_content_length"
)

var (
	defaultCompressibleTypes = []string{
		"*font*",
		"*javascript",
		"*json",
		"*ml;*",
		"*mpegURL",
		"*mpegurl",
		"*otf",
		"*ttf",
		"*type",
		"*xml",
		"application/eot",
		"application/pkix-crl",
		"application/x-httpd-cgi",
		"application/x-perl",
		"image/vnd.microsoft.icon",
		"image/x-icon",
		"text/*",
	}
)

func init() {
	voluspa.AdaptersRegistry.AddAdapter(&GzipAdapter{})
}

var (
	GzipDefaultAlgorithms = []string{"gzip"}
)

type GzipAdapter struct {
}

func (p *GzipAdapter) PluginType() voluspa.PluginType {
	return voluspa.GeneralAdapter
}

func (p *GzipAdapter) Type() voluspa.AdapterType {
	return voluspa.AdapterType("gzip")
}

func (p *GzipAdapter) Name() string {
	return "gzip"
}

func (p *GzipAdapter) SharedLibraryName() string {
	return "gzip.so"
}

func (p *GzipAdapter) ConfigLocations() []voluspa.ConfigLocation {
	return []voluspa.ConfigLocation{voluspa.ParentConfig}
}

func (p *GzipAdapter) ConfigParameters() []string {
	return []string{GzipAdapterParameter}
}

func (p *GzipAdapter) Content(env *voluspa.ConfigEnvironment) (*bytes.Buffer, error) {
	content := &bytes.Buffer{}

	config, err := env.RemapOptions.ValueByNameAsStringMapInterface(GzipAdapterParameter)
	if err != nil {
		return nil, err
	}

	{
		value, hasOption := config[GzipAdapterParameterEnabled]
		if hasOption {
			bvalue, ok := value.(bool)
			if !ok {
				return nil, fmt.Errorf("invalid 'enabled' value (%s): %t", value, value)
			}

			if !bvalue {
				return content, nil
			}
		}
	}

	var algorithms []string
	avalue, hasOption := config[GzipAdapterParameterAlgorithms]
	if hasOption {
		sliceValue, ok := avalue.([]interface{})
		if !ok {
			return nil, fmt.Errorf("invalid value for %q: (%s) %t", GzipAdapterParameterAlgorithms, avalue, avalue)
		}

		if len(sliceValue) > 0 {
			for _, a := range sliceValue {
				value := a.(string)
				switch value {
				case "gzip":
					fallthrough
				case "br":
					algorithms = append(algorithms, value)
				default:
					return nil, fmt.Errorf("invalid value for %q: (%s)", GzipAdapterParameterAlgorithms, value)
				}
			}
		}
	} else {
		algorithms = GzipDefaultAlgorithms
	}
	sort.Sort(sort.StringSlice(algorithms))

	var compressibleTypes []string
	params, found := config[GzipAdapterParameterCompressibleContentType]
	if found {
		typesSlice, ok := params.([]interface{})
		if !ok {
			return nil, fmt.Errorf("invalid value for %q: (%s) %t", GzipAdapterParameterCompressibleContentType, params, params)
		}

		for _, a := range typesSlice {
			compressibleTypes = append(compressibleTypes, a.(string))
		}
	} else {
		compressibleTypes = defaultCompressibleTypes
	}

	sort.Sort(sort.StringSlice(compressibleTypes))

	content.WriteString(`enabled true
cache false
`)

	value, hasOption := config[GzipAdapterParameterRemoveAcceptEncoding]
	if hasOption {
		on, ok := value.(bool)
		if ok && on {
			content.WriteString("remove-accept-encoding true\n")
		}
	}

	value, hasOption = config[GzipAdapterParameterFlush]
	if hasOption {
		on, ok := value.(bool)
		if ok && on {
			content.WriteString("flush true\n")
		}
	}

	value, hasOption = config[GzipAdapterParameterMinimumContentLength]
	if hasOption {
		minLen, ok := value.(int)
		if ok && minLen > 0 {
			content.WriteString(fmt.Sprintf("minimum-content-length %d\n", minLen))
		}
	}

	content.WriteString(fmt.Sprintf("supported-algorithms %s\n", strings.Join(algorithms, ",")))

	for _, param := range compressibleTypes {
		content.WriteString(fmt.Sprintf("compressible-content-type %s\n", param))
	}

	return content, nil
}
