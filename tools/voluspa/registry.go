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

package voluspa

import (
	"fmt"
	"log"
)

var AdaptersRegistry *AdapterRegistry

func init() {
	AdaptersRegistry = NewAdapterRegistry()
}

// AdapterRegistry is the central registry for all plugin config plugins
type AdapterRegistry struct {
	adapters             map[AdapterType]Adapter
	compoundAdapters     map[AdapterType][]Adapter
	configNameAdapterMap map[string]AdapterType
	fileNameMap          map[string]AdapterType
}

func NewAdapterRegistry() *AdapterRegistry {
	return &AdapterRegistry{
		adapters:             make(map[AdapterType]Adapter),
		compoundAdapters:     make(map[AdapterType][]Adapter),
		configNameAdapterMap: make(map[string]AdapterType),
		fileNameMap:          make(map[string]AdapterType),
	}
}

func (r *AdapterRegistry) AddAdapter(p Adapter) {
	if p.PluginType() == CompoundAdapter {
		r.compoundAdapters[p.Type()] = append(r.compoundAdapters[p.Type()], p)
	} else {
		r.adapters[p.Type()] = p
	}

	for _, v := range p.ConfigParameters() {
		if v == "" {
			continue
		}
		if previous, ok := r.configNameAdapterMap[v]; !ok {
			r.configNameAdapterMap[v] = p.Type()
		} else {
			log.Printf("Duplicate config parameter: %s for %s. Previous AdapterType=%s", v, p.Type(), previous)
		}
	}

	scp, ok := p.(SubConfigAdapter)
	if !ok {
		return
	}
	if scp.Name() == "" {
		return
	}
	if previous, ok := r.fileNameMap[scp.Name()]; !ok {
		r.fileNameMap[scp.Name()] = p.Type()
	} else {
		log.Printf("Duplicate subconfig filename: %s for %s. Previous AdapterType=%s", p.Type(), scp.Name(), previous)
	}
}

func (r *AdapterRegistry) adapterForType(t AdapterType) Adapter {
	vp := r.adapters[t]
	if vp != nil {
		return vp
	}

	plugins := r.compoundAdapters[t]
	if len(plugins) > 0 {
		return plugins[0]
	}
	return nil
}

func (r *AdapterRegistry) RemoveAdapterByType(adapterType string) error {
	at := AdapterType(adapterType)

	var adapter Adapter

	nullAdapter, found := r.adapters[AdapterType("null")]
	if !found {
		return fmt.Errorf("%q adapter not found", "null")
	}

	adapter, found = r.adapters[at]
	if found {
		r.adapters[at] = nullAdapter
	}

	if !found {
		return fmt.Errorf("unknown adapter type %q", adapterType)
	}

	// override keywords
	for _, v := range adapter.ConfigParameters() {
		r.configNameAdapterMap[v] = nullAdapter.Type()
	}

	return nil
}

func (r *AdapterRegistry) adaptersForType(t AdapterType) []Adapter {
	return r.compoundAdapters[t]
}

func (r *AdapterRegistry) adapterTypeByConfigName(key string) AdapterType {
	v, ok := r.configNameAdapterMap[key]
	if !ok {
		return UnknownAdapter
	}
	return v
}
