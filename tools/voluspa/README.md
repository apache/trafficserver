# Voluspa

### Apache Traffic Server Configuration Tool (experiment)

A tool to codify the CDN feature set and make it easier and more reliable to setup new CDN properties.

Configuration files are well-formed YAML files that express the desired behaviour.


## Usage
```
$ ./voluspa -dest=ourconfigs myproperty.conf

$ find ourconfigs -type f
ourconfigs/ssl_multicert.config_tenant
ourconfigs/myproperty/myproperty.config
ourconfigs/myproperty/hdrs.config
ourconfigs/ssl_multicert.config_default
ourconfigs/voluspa.sls
```

The voluspa configuration language is defined by a
[JSON Schema](https://spacetelescope.github.io/understanding-json-schema/index.html)
stored in the [schema file](schema_v1.json).

Tool will generally error out if something is wrong (missing required fields) or weirdly configured options.
Warnings will be generated for unknown configuration options (but will attempt to still process the file).
There are a number of options to control the output and output location (see -help)

By default, all configs get a receipt header and propstats enabled unless disabled via the appropriate option.

## Extending

### General process includes:
- adding the shared library name (sharedLibraryNames)
- configuration option name (see pluginConfigNameMap)
- weighting the plugin (for sorting) (pluginWeights)
- most importantly, the class/struct/file for the plugin
- a bit more complicated if it's a "compound plugin" (eg HeaderRewrite)


## Tests
There are a couple of go-based unit tests (no where near comprehensive) and there's what I would call sanity tests in tests/. The script "test.sh" will use the current build of the tool to regenerate the remap configs for a given yaml file; and do a diff between base/ and the new output. Generally, there should be no changes between runs. If there are expected changes, the baselines would need to be regenerated. The configurations tested range from very simple to something exercising all current configuration options.


## TODO
- error on mutually exclusive options (eg allow\_ip and deny\_methods)
