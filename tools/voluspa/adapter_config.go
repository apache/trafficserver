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
	"bytes"
	"fmt"
	"sort"
	"strings"
)

type AdapterType string

const (
	UnknownAdapter AdapterType = "unknown"
	Receipt                    = "receipt"
)

type ConfigLocation int

const (
	UnknownLocation ConfigLocation = iota
	ChildConfig
	ParentConfig
)

// validConfigrationOptions are options that are not plugin specific (which are managed by the plugin)
var validConfigurationOptions = map[string]bool{
	"parent_child": true,
	Parent:         true,
	Child:          true,
}

type mappingRule struct {
	Type              AdapterType
	Weight            int
	SubConfigFileName string
	SubConfigContent  *bytes.Buffer
	ConfigContent     *bytes.Buffer
}

func newMappingRule(vp Adapter) *mappingRule {
	return &mappingRule{Type: vp.Type(), Weight: getWeight(vp)}
}

// hasContent returns true if there's configuration content, false otherwise
func (p *mappingRule) hasContent() bool {
	return (p.SubConfigContent != nil && p.SubConfigContent.Len() > 0) ||
		(p.ConfigContent != nil && p.ConfigContent.Len() > 0)
}

func (p *mappingRule) isCommandLineTemplated() bool {
	return p.ConfigContent != nil && strings.Contains(p.ConfigContent.String(), "{%")
}

// pluginWeights affects the ordering of the configuration output.
// Only add to this list if you have hard ordering requirements, otherwise just stick
// with the default weighting (5)
func getWeight(vp Adapter) int {
	t, ok := vp.(Weighty)
	if !ok {
		return 5
	}
	return t.Weight()
}

// SubConfigFilename returns the subconfig filename given plugin and property name
func SubConfigFilename(p SubConfigAdapter, env *ConfigEnvironment) string {
	extension := "config"
	name := p.Name()
	if strings.HasSuffix(p.Name(), ".lua") {
		extension = "lua"
		name = strings.TrimSuffix(name, ".lua")
	}
	if env.id == 1 {
		if env.ConfigLocation == ParentConfig {
			return fmt.Sprintf("%s_parent.%s", name, extension)
		}
		return fmt.Sprintf("%s.%s", name, extension)
	}

	if env.ConfigLocation == ParentConfig {
		return fmt.Sprintf("%s%d_parent.%s", name, env.id, extension)
	}
	return fmt.Sprintf("%s%d.%s", name, env.id, extension)
}

// ConfigFileNameSpecification appends to buffer the sub config filename configuration specification
// nolint: interfacer
func ConfigFileNameSpecification(p Adapter, env *ConfigEnvironment, buffer *bytes.Buffer) (*bytes.Buffer, error) {
	var configFileSwitch string
	cfs, ok := p.(ConfigFileSwitch)
	if ok {
		configFileSwitch = cfs.ConfigFileSwitch()
	}

	scp, ok := p.(SubConfigAdapter)
	if ok {
		fmt.Fprintf(buffer, " @pparam=%s%s/%s", configFileSwitch, strings.ToLower(env.Property), SubConfigFilename(scp, env))
	}
	return buffer, nil
}

func isParameterBasedAdapter(p Adapter) bool {
	_, ok := p.(ParameterAdapter)
	if ok {
		return true
	}

	_, ok = p.(RoleifiedParameterAdapter)

	return ok
}

func ConfigContent(p Adapter, env *ConfigEnvironment) (*bytes.Buffer, error) {

	var buffer bytes.Buffer

	var roleEnabled bool
	rep, ok := p.(RoleEnabled)
	if ok {
		roleEnabled = true
		fmt.Fprintf(&buffer, "{%% if salt.pillar.get(\"%s\") %%}%s", rep.Role(), remapPrefixBuffer)
	}

	sla, ok := p.(SharedLibraryAdapter)
	if ok {
		fmt.Fprintf(&buffer, "@plugin=%s", sla.SharedLibraryName())
	}

	if !isParameterBasedAdapter(p) {
		return ConfigFileNameSpecification(p, env, &buffer)
	}

	var err error
	var subfer *bytes.Buffer
	switch vpp := p.(type) {
	case ParameterAdapter:
		subfer, err = processParameterAdapter(p, env, vpp)
	case RoleifiedParameterAdapter:
		subfer, err = processRoleifiedParameterAdapter(vpp, env)
	default:
		return nil, fmt.Errorf("unhandled adapter %s", vpp)
	}

	if err != nil {
		return nil, err
	}

	// if sub-buffer is nil, pass back nil
	if subfer == nil {
		return nil, nil
	}

	buffer.Write(subfer.Bytes())

	if roleEnabled {
		fmt.Fprintf(&buffer, " \\{%% else %%}%s\\{%% endif %%}", remapPrefixBuffer)
		fmt.Fprintln(&buffer, "")
	}

	return &buffer, nil
}

func processParameterAdapter(p Adapter, env *ConfigEnvironment, vpp ParameterAdapter) (*bytes.Buffer, error) {
	var buffer bytes.Buffer

	// throw away all work if we get an error (which can be a signal not to include this plugin (see propstats))
	// TODO: signal with appropriate error (like ErrSkipConfig)
	pparams, err := vpp.PParams(env)
	if err != nil {
		return &buffer, err
	}

	raw := false
	rpparams, ok := vpp.(RawParameterAdapter)
	if ok {
		raw = rpparams.Raw()
	}

	if len(pparams) == 0 {
		if p.PluginType() == CompoundAdapter {
			return &buffer, nil
		}
		// no buffer to append?
		return nil, nil
	}

	if raw {
		for _, val := range pparams {
			fmt.Fprintf(&buffer, " %s", val)
		}
	} else {
		for _, val := range pparams {
			fmt.Fprintf(&buffer, " @pparam=%s", val)
		}
	}

	return &buffer, nil
}

func processRoleifiedParameterAdapter(vpp RoleifiedParameterAdapter, env *ConfigEnvironment) (*bytes.Buffer, error) {
	var buffer bytes.Buffer
	pparams, err := vpp.PParams(env)
	if err != nil {
		return &buffer, err
	}

	// extract non-Default role keys from parameter list
	var roleKeys []string
	for key := range pparams {
		if key == DefaultRole {
			continue
		}
		roleKeys = append(roleKeys, key)
	}
	sort.Strings(roleKeys)

	// 2 passes
	// 1. shove the pparams and role-guards into a list
	// 2. iterate over that listen, building up buffer with the \n and \\ and whitespace

	var args []string
	pparam, found := pparams[DefaultRole]
	if found {
		for _, val := range pparam {
			args = append(args, fmt.Sprintf("@pparam=%s", val))
		}
	}

	for _, key := range roleKeys {
		args = append(args, fmt.Sprintf("{%% if salt.pillar.get(\"%s\") %%}%s", key, remapPrefixBuffer))

		for _, val := range pparams[key] {
			args = append(args, fmt.Sprintf("@pparam=%s", val))
		}
		args = append(args, fmt.Sprintf(" \\{%% else %%}%s\\{%% endif %%}", remapPrefixBuffer))
	}

	// peel off last arg for special handling
	lastArg := args[len(args)-1]
	args = args[:len(args)-1]

	for _, arg := range args {
		fmt.Fprintf(&buffer, " %s", arg)
	}

	// TODO: assuming last element is {% endif %} block
	fmt.Fprintf(&buffer, "%s\n", lastArg)

	return &buffer, nil
}

func SubConfigContent(p Adapter, env *ConfigEnvironment) (*bytes.Buffer, error) {
	cba, ok := p.(ContentBasedAdapter)
	if ok {
		return cba.Content(env)
	}
	return nil, nil
}

// IsValidConfigurationOption returns true if configuration key is valid
func IsValidConfigurationOption(option string) bool {
	return validConfigurationOptions[option]
}
