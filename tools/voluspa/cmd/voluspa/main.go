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

package main

import (
	"flag"
	"fmt"
	"log"
	"os"
	"sort"
	"strings"

	"github.com/apache/trafficserver/tools/voluspa"
	_ "github.com/apache/trafficserver/tools/voluspa/adapters"
)

var (
	configDest       = flag.String("dest", "out", "destination directory for written out configs")
	installLocation  = flag.String("install", voluspa.DefaultTrafficserverConfigurationDir, "trafficserver configuration location")
	defaultsLocation = flag.String("defaults", "", "location of default configurations")
	schemaLocation   = flag.String("schema-location", "", "location of schema (defaults to cwd)")
	disableAdapters  = flag.String("disable-adapters", "", "a comma separated list of adapters to disable")

	cdnList              = flag.String("cdns", "", "a comma separated list of CDNs")
	roleList             = flag.String("roles", "", "a comma separated list of roles")
	showVersion          = flag.Bool("version", false, "Show version")
	verbose              = flag.Bool("verbose", false, "Verbose output")
	skipSchemaValidation = flag.Bool("skip-schema-validation", false, "skip schema validation")
	treatRolesAsCDN      = flag.Bool("promote-roles-to-cdn", false, "promote roles to CDN")
	ramDiskRole          = flag.String("ramdisk-role", "roles_trafficserver_ramdisk", "The role that indicates this host has a ramdisk volume")

	skipValidation    = flag.Bool("skip-validation", false, "skip validation")
	validationOnly    = flag.Bool("validate-only", false, "stop after validation")
	strictMode        = flag.Bool("strict", false, "enable strict validation")
	dumpSchemaVersion = flag.Int("dump-schema", -1, "dump this version of the schema (0 = highest) to STDOUT and exit 0")
)

func init() {
	log.SetFlags(0)

	flag.Parse()
}

var exit = func(code int) {
	os.Exit(code)
}

func main() {
	if *showVersion {
		fmt.Println("voluspa", voluspa.Version)
		exit(0)
	}

	v, err := voluspa.NewVoluspaWithOptions(&voluspa.Options{
		Verbose:              *verbose,
		Destination:          *configDest,
		SkipSchemaValidation: *skipSchemaValidation,
		PromoteRolesToCDN:    *treatRolesAsCDN,
		DefaultsLocation:     *defaultsLocation,
		SchemaLocation:       *schemaLocation,
		RamDiskRole:          *ramDiskRole,
	}, *installLocation)
	if err != nil {
		log.Printf("%s", err)
		exit(1)
	}

	if *dumpSchemaVersion >= 0 {
		schema, err := v.SchemaDefinition(*dumpSchemaVersion)
		if err != nil {
			log.Printf("%s", err)
			exit(1)
		}
		fmt.Fprint(os.Stdout, string(schema))
		exit(0)
	}

	if len(*disableAdapters) > 0 {
		adapters := strings.Split(*disableAdapters, ",")
		for _, adapter := range adapters {
			if err = voluspa.AdaptersRegistry.RemoveAdapterByType(adapter); err != nil {
				log.Fatalf("Could not disable adapter %q: %s", adapter, err)
			}
		}
	}

	if len(flag.Args()) == 0 {
		log.Printf("Must specify at least 1 config file")
		exit(1)
	}

	filenames := flag.Args()
	sort.Strings(filenames)

	for _, filename := range filenames {
		err = v.AddConfig(filename)
		if err != nil {
			log.Printf("%s", err)
			exit(1)
		}
	}

	if !*skipValidation {
		if err = v.Validate(*strictMode); err == nil {
			goto validationDone
		}

		fmt.Fprintf(os.Stderr, "voluspa: errors validating configs:\n")

		errs, ok := err.(voluspa.Errors)
		if !ok {
			fmt.Fprintln(os.Stderr, err)
			exit(1)
		}

		// sort and unique-ify the set of errors
		// like errs.Error() but also uniques and has different format
		seen := make(map[string]interface{})
		var errOut []string
		for _, e := range errs {
			if _, found := seen[e.Error()]; found {
				continue
			}
			errOut = append(errOut, fmt.Sprintf("    - %s", e.Error()))
			seen[e.Error()] = nil
		}

		sort.Strings(errOut)

		for _, str := range errOut {
			fmt.Fprintln(os.Stderr, str)
		}
		exit(1)
	}

validationDone:
	if *validationOnly {
		exit(0)
	}

	var cdns []string
	if len(*cdnList) > 0 {
		cdns = strings.Split(*cdnList, ",")
	}

	var roles []string
	if len(*roleList) > 0 {
		roles = strings.Split(*roleList, ",")
	}

	if err = v.WriteAllFiles(cdns, roles); err != nil {
		log.Printf("%s", err)
		exit(1)
	}
}
