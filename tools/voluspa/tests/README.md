# Tests

This directory contains a series of sanity tests. These are simple tests comparing the output of a known good build/configuration to the latest locally built version of **voluspa**.

The script "test.sh" will use the current build of the tool to regenerate the remap configs for a given yaml file; and do a diff between base/ and the new output. Generally, there should be no changes between runs. If there are expected changes, the baselines would need to be regenerated. The configurations tested range from very simple to something exercising all current configuration options.

## Current Tests

* simple/
	* Basic functionality
* all/
	* Every current configuration option exercising combinations of config types (normal and explicit)
* order/
	*  Covers both ordering of properties by QPS and grouping via "role"
* parentchild/
	* A baseline of current parent/child config generation
