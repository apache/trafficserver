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
	ReceiptsAdapterParameter        = "receipts"
	ReceiptsAdapterParameterEnabled = "enabled"
	ReceiptsAdapterParameterName    = "name"
)

func init() {
	voluspa.AdaptersRegistry.AddAdapter(&ReceiptAdapter{})
}

type ReceiptAdapter struct {
}

func (*ReceiptAdapter) Weight() int {
	return 25
}

func (p *ReceiptAdapter) PluginType() voluspa.PluginType {
	return voluspa.CompoundAdapter
}

func (p *ReceiptAdapter) Type() voluspa.AdapterType {
	return voluspa.Receipt
}

func (p *ReceiptAdapter) CompoundType() voluspa.AdapterType {
	return adapterType
}

func (p *ReceiptAdapter) Name() string {
	return ""
}

func (p *ReceiptAdapter) ConfigParameters() []string {
	return []string{ReceiptsAdapterParameter}
}

func (p *ReceiptAdapter) Content(env *voluspa.ConfigEnvironment) (*bytes.Buffer, error) {
	content := &bytes.Buffer{}

	receiptsConfig, err := env.RemapOptions.ValueByNameAsStringMapInterface(ReceiptsAdapterParameter)
	if err != nil {
		return nil, err
	}

	value, hasOption := receiptsConfig[ReceiptsAdapterParameterEnabled]
	if hasOption {
		bvalue, ok := value.(bool)
		if !ok {
			return nil, fmt.Errorf("invalid 'enabled' value (%s)", value)
		}

		if !bvalue {
			return content, nil
		}
	}

	receiptName := env.Property
	value, hasOption = receiptsConfig[ReceiptsAdapterParameterName]
	if hasOption {
		strvalue, ok := value.(string)
		if !ok {
			return nil, fmt.Errorf("invalid 'name' value (%s)", value)
		}
		receiptName = strvalue
	}

	receipt := fmt.Sprintf("%s{{hosttype}}", receiptName)
	content.WriteString(util.FormatSimpleHeaderRewrite("REMAP_PSEUDO_HOOK", fmt.Sprintf(`set-header @ReceiptService "%s"`, receipt)))

	return content, nil
}
