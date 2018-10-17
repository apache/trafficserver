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

	"github.com/apache/trafficserver/tools/voluspa"
)

func init() {
	voluspa.AdaptersRegistry.AddAdapter(&BaseHeaderRewriteAdapter{})
}

type BaseHeaderRewriteAdapter struct {
}

func (*BaseHeaderRewriteAdapter) Weight() int {
	return 25
}

func (p *BaseHeaderRewriteAdapter) PluginType() voluspa.PluginType {
	return voluspa.CompoundAdapter
}

func (p *BaseHeaderRewriteAdapter) Type() voluspa.AdapterType {
	return adapterType
}

func (p *BaseHeaderRewriteAdapter) CompoundType() voluspa.AdapterType {
	return adapterType
}

func (p *BaseHeaderRewriteAdapter) Name() string {
	return "hdrs"
}

func (p *BaseHeaderRewriteAdapter) SharedLibraryName() string {
	return "header_rewrite.so"
}

func (p *BaseHeaderRewriteAdapter) ConfigParameters() []string {
	return []string{""}
}

func (p *BaseHeaderRewriteAdapter) Content(env *voluspa.ConfigEnvironment) (*bytes.Buffer, error) {
	return &bytes.Buffer{}, nil
}
