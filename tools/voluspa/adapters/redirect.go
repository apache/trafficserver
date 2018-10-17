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

	"github.com/apache/trafficserver/tools/voluspa"
)

func init() {
	voluspa.AdaptersRegistry.AddAdapter(&RedirectAdapter{})
}

type RedirectAdapter struct {
}

func (*RedirectAdapter) Weight() int {
	return 30
}

func (p *RedirectAdapter) PluginType() voluspa.PluginType {
	return voluspa.GeneralAdapter
}

func (p *RedirectAdapter) Type() voluspa.AdapterType {
	return voluspa.AdapterType("redirect")
}

func (p *RedirectAdapter) Name() string {
	return "redirect"
}

func (p *RedirectAdapter) SharedLibraryName() string {
	return regexRemapSharedLibraryName
}

func (p *RedirectAdapter) ConfigParameters() []string {
	return []string{"redirect"}
}

func (p *RedirectAdapter) Content(env *voluspa.ConfigEnvironment) (*bytes.Buffer, error) {
	content := &bytes.Buffer{}
	urls, err := env.RemapOptions.ValueByNameAsStringMapInterface(p.Name())
	if err != nil {
		return nil, err
	}

	url, ok := urls["url"].(string)
	if !ok {
		return nil, fmt.Errorf("Expecting url as string and Redirect url is mandatory")
	}

	// default status code to 302 if not provided
	httpCode, ok := urls["http_code"].(int)
	if !ok {
		httpCode = 302
	}

	// configurable source regex pattern defaults to (.*)
	src, ok := urls["src"]
	if !ok {
		src = "(.*)"
	}

	content.WriteString(fmt.Sprintf("%s %s @status=%d", src, url, httpCode))

	return content, nil
}
