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
	"io/ioutil"
	"net/url"
	"os"
	"regexp"
	"sort"
	"unicode"

	yaml "gopkg.in/yaml.v2"
)

// PropertyConfig represents the raw parsed structure from YAML
type PropertyConfig struct {
	Owner            string         `json:"owner,omitempty" yaml:",omitempty"`
	Reference        string         `json:"reference,omitempty" yaml:",omitempty"`
	Lifecycle        LifecycleState `json:"lifecycle,omitempty" yaml:",omitempty"`
	Role             string         `json:"role,omitempty" yaml:",omitempty"`
	Property         string         `json:"rcpt,omitempty" yaml:"rcpt,omitempty"`
	SchemaVersion    string         `json:"schema_version,omitempty" yaml:"schema_version,omitempty"`
	SSLCertNames     []string       `json:"ssl_cert_names,omitempty" yaml:"ssl_cert_names,omitempty"`
	Schemes          []string       `json:"schemes,omitempty" yaml:",omitempty"`
	CDN              []string       `json:"cdn,omitempty" yaml:",omitempty"`
	Org              string         `json:"org,omitempty" yaml:"org,omitempty"`
	Rules            map[string]RemapOptions
	ExplicitMappings []*Mapping         `json:"explicit,omitempty" yaml:"explicit,omitempty"`
	Mappings         []*Mapping         `json:"mappings,omitempty" yaml:"mappings,omitempty"`
	ParentChild      bool               `json:"parent_child,omitempty" yaml:"parent_child,omitempty"`
	QPS              int                `json:"qps" yaml:"qps"`
	HAProxyVIPs      []HAProxyVIP       `json:"ha_proxy,omitempty" yaml:"ha_proxy,omitempty"`
	Tests            map[string]CDNTest `json:"tests,omitempty" yaml:"tests,omitempty"`
	Receiptsd        *ReceiptsdConfig   `json:"receiptsd,omitempty" yaml:"receiptsd,omitempty"`
}

type Mapping struct {
	Origin  string   `json:"origin,omitempty" yaml:"origin,omitempty"`
	Group   string   `json:"group,omitempty" yaml:"group,omitempty"`
	Role    string   `json:"role,omitempty" yaml:"role,omitempty"`
	Alias   []string `json:"alias,omitempty" yaml:"alias,omitempty"`
	Schemes []string `json:"schemes,omitempty" yaml:"schemes,omitempty"`

	RuleName         string           `json:"rule" yaml:"rule"`
	ParentDestDomain string           `json:"parent_dest_domain,omitempty" yaml:"parent_dest_domain,omitempty"`
	ParentOverride   []ParentOverride `json:"parent_override,omitempty" yaml:"parent_override,omitempty"`
	RegexMap         bool             `json:"regex_map" yaml:"regex_map"`
	RemapOptions     RemapOptions     `json:",omitempty" yaml:",omitempty"`
	explicit         bool
	id               int
}

type ParentOverride struct {
	Role              string   `json:"role,omitempty" yaml:"role,omitempty"`
	DestDomain        string   `json:"dest_domain,omitempty" yaml:"dest_domain"`
	PrimaryList       []string `json:"primary_list,omitempty" yaml:"primary_list,omitempty"`
	SecondaryList     []string `json:"secondary_list,omitempty" yaml:"secondary_list,omitempty"`
	Strategy          string   `json:"strategy,omitempty" yaml:"strategy,omitempty"`
	IgnoreQuerystring bool     `json:"ignore_querystring,omitempty" yaml:"ignore_querystring,omitempty"`
	HTTPPort          int      `json:"http_port,omitempty" yaml:"http_port,omitempty"`
	HTTPSPort         int      `json:"https_port,omitempty" yaml:"https_port,omitempty"`
	GoDirect          bool     `json:"go_direct,omitempty" yaml:"go_direct,omitempty"`
}

func (m *Mapping) ParentDomain() string {

	// a po without a role but with a domain
	for _, po := range m.ParentOverride {
		if po.Role == "" && len(po.DestDomain) > 0 {
			return po.DestDomain
		}
	}

	// Deprecated
	if len(m.ParentDestDomain) > 0 {
		return m.ParentDestDomain
	}
	// the domain is not overriden, old style or new style, return the origin domain
	parentURL, err := url.Parse(m.Origin)
	if err == nil && len(parentURL.Scheme) > 0 {
		return parentURL.Host
	}

	return m.Origin
}

const (
	NoCDN              = ""
	NoRole             = ""
	DefaultCDN         = "edge"
	DefaultScheme      = "http"
	DefaultRulesetName = "default"
)

var propertyNameRE = regexp.MustCompile(`^[-_A-Za-z0-9\.]+$`)

func isValidPropertyName(in string) bool {
	return propertyNameRE.MatchString(in)
}

// Setup validates and sets up a PropertyConfig
func (p *PropertyConfig) Setup() error {

	if len(p.Schemes) > 0 {
		for _, scheme := range p.Schemes {
			if !isValidScheme(scheme) {
				return fmt.Errorf("invalid/unsupported scheme %q", scheme)
			}
		}
	}

	// rcpt is the public-facing name of the field, Property is the internal name
	if len(p.Property) == 0 {
		return fmt.Errorf("rcpt is required")
	}

	if !isValidPropertyName(p.Property) {
		return fmt.Errorf("property name can only contain alphanumeric characters")
	}

	// assign an id for each rule name
	// sort keys/path for stable ids
	matchIds := make(map[string]int)
	{
		var keys []string
		for k := range p.Rules {
			keys = append(keys, k)
		}
		sort.Strings(keys)

		i := 1
		for _, k := range keys {
			matchIds[k] = i
			i++
		}
	}

	for _, m := range p.Mappings {
		if m.RuleName == "" {
			if _, ok := p.Rules["default"]; ok {
				m.RuleName = "default"
			}
		}

		if m.Group == "" {
			m.Group = DefaultGroup
		}

		m.id = matchIds[m.RuleName]

		if ruleset, ok := p.Rules[m.RuleName]; ok {
			m.RemapOptions = ruleset
		} else {
			return fmt.Errorf("invalid ruleset name: %q for origin %q", m.RuleName, m.Origin)
		}

		if len(m.Alias) == 0 {
			return fmt.Errorf("no alias specified for origin %q", m.Origin)
		}
		if hasLoop(m) {
			return fmt.Errorf("alias and origin %q will loop", m.Origin)
		}
	}

	for _, m := range p.ExplicitMappings {
		if m.RuleName == "" {
			if _, ok := p.Rules[DefaultRulesetName]; ok {
				m.RuleName = DefaultRulesetName
			}
		}

		if m.Group == "" {
			m.Group = DefaultGroup
		}

		m.id = matchIds[m.RuleName]

		if ruleset, ok := p.Rules[m.RuleName]; ok {
			m.RemapOptions = ruleset
		} else {
			return fmt.Errorf("invalid ruleset name: %q for origin %q", m.RuleName, m.Origin)
		}

		if len(m.Alias) == 0 {
			return fmt.Errorf("no alias specified for origin %q", m.Origin)
		}
	}

	return nil
}

func hasLoop(m *Mapping) bool {
	for _, scheme := range m.schemes() {
		for _, incoming := range m.Alias {
			incomingURL := formatURL(scheme, incoming)
			originURL := formatURL(scheme, m.Origin)
			if incomingURL == originURL {
				return true
			}
		}
	}
	return false
}

func (p *PropertyConfig) loadDefaultConfig(defaultsLocation, filename string) error {
	var fullPath string
	if len(defaultsLocation) > 0 {
		fullPath = fmt.Sprintf("%s/%s", defaultsLocation, filename)
	} else {
		fullPath = filename
	}
	if _, err := os.Stat(fullPath); os.IsNotExist(err) {
		return nil
	}

	cfgContents, err := ioutil.ReadFile(fullPath)
	if err != nil {
		return err
	}

	return p.parseConfig(cfgContents)
}

func isASCII(in string) (bool, rune) {
	for _, c := range in {
		if c > unicode.MaxASCII {
			return false, c
		}
	}
	return true, 0
}

func (p *PropertyConfig) parseConfig(cfgContents []byte) error {
	valid, char := isASCII(string(cfgContents))
	if !valid {
		return fmt.Errorf("non-ASCII characters not supported: invalid rune %q", string(char))
	}

	return yaml.Unmarshal(cfgContents, &p)
}

func (p *PropertyConfig) mergeConfig(c *PropertyConfig) {
	overrides, ok := c.Rules[DefaultRulesetName]
	if !ok {
		return
	}

	for _, ruleset := range p.Rules {
		if ruleset == nil {
			continue
		}
		for k, v := range overrides {
			if _, exists := ruleset[k]; !exists {
				ruleset[k] = v
			}
		}
	}
}

func newPropertyConfig() *PropertyConfig {
	return &PropertyConfig{
		Rules:     make(map[string]RemapOptions),
		Schemes:   []string{DefaultScheme},
		CDN:       []string{DefaultCDN},
		Lifecycle: Live,
	}
}

// NewPropertyConfig creates a new PropertyConfig from a buffer containing a YAML file
func NewPropertyConfig(buffer []byte) (*PropertyConfig, error) {
	return NewPropertyConfigWithDefaults(buffer, "")
}

// NewPropertyConfigWithDefaults creates a new PropertyConfig from a buffer containing a YAML file
func NewPropertyConfigWithDefaults(buffer []byte, defaultsLocation string) (*PropertyConfig, error) {
	cfg := newPropertyConfig()

	if err := cfg.parseConfig(buffer); err != nil {
		return nil, fmt.Errorf("Could not load YAML file. err=%s", err)
	}

	return inflatePropertyConfig(cfg, defaultsLocation)
}

func inflatePropertyConfig(cfg *PropertyConfig, defaultsLocation string) (*PropertyConfig, error) {
	dcfg := newPropertyConfig()

	if err := dcfg.loadDefaultConfig(defaultsLocation, "default.conf"); err != nil {
		return nil, fmt.Errorf("Could not load %q: %s", "defaults/default.conf", err)
	}

	cfg.mergeConfig(dcfg)

	if cfg.ParentChild {
		dpCfg := newPropertyConfig()

		if err := dpCfg.loadDefaultConfig(defaultsLocation, "default_parent.conf"); err != nil {
			return nil, fmt.Errorf("Could not load %q: %s", "defaults/default_parent.conf", err)
		}
		cfg.mergeConfig(dpCfg)
	}

	if err := cfg.Setup(); err != nil {
		return nil, err
	}
	return cfg, nil
}
