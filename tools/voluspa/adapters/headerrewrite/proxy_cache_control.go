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

package headerrewrite

import (
	"bytes"
	"fmt"

	"github.com/apache/trafficserver/tools/voluspa"
	"github.com/apache/trafficserver/tools/voluspa/adapters/util"
)

const (
	ProxyCacheControlAdapterParameter = "proxy_cache_control"
)

func init() {
	voluspa.AdaptersRegistry.AddAdapter(&ProxyCacheControlAdapter{})
}

type ProxyCacheControlAdapter struct {
}

func (p *ProxyCacheControlAdapter) PluginType() voluspa.PluginType {
	return voluspa.CompoundAdapter
}

func (p *ProxyCacheControlAdapter) Type() voluspa.AdapterType {
	return voluspa.AdapterType("proxy_cache_control")
}

func (p *ProxyCacheControlAdapter) CompoundType() voluspa.AdapterType {
	return adapterType
}

func (p *ProxyCacheControlAdapter) ConfigParameters() []string {
	return []string{ProxyCacheControlAdapterParameter}
}

func (p *ProxyCacheControlAdapter) Content(env *voluspa.ConfigEnvironment) (*bytes.Buffer, error) {
	header, err := env.RemapOptions.ValueByNameAsString(ProxyCacheControlAdapterParameter)
	if err != nil {
		return nil, util.FormatError(ProxyCacheControlAdapterParameter, err)
	}

	content := &bytes.Buffer{}

	content.WriteString(fmt.Sprintf(
		`cond %%{READ_RESPONSE_HDR_HOOK}
    set-header @Original-Cache-Control %%{HEADER:Cache-Control}
    set-header Cache-Control %%{HEADER:%s}

cond %%{SEND_RESPONSE_HDR_HOOK}
    set-header Cache-Control %%{HEADER:@Original-Cache-Control}
    rm-header Proxy-Cache-Control`,
		header,
	))

	return content, nil
}
