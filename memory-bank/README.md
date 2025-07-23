# Apache Traffic Server Memory Bank

## Overview

This memory bank contains comprehensive documentation about the Apache Traffic Server project, organized into focused sections for easy reference.

This document follows best practices from https://docs.cline.bot/prompting/cline-memory-bank .

## Project Summary

**Apache Traffic Server (ATS)** is a high-performance, open-source HTTP/HTTPS caching proxy server and web application acceleration platform designed for cloud services and large-scale web applications.

- **Version**: 10.2.0 (current)
- **Language**: C++20
- **Build System**: CMake
- **License**: Apache License 2.0
- **GitHub**: https://github.com/apache/trafficserver

## Memory Bank Structure

### [01 - Project Overview](01-project-overview.md)
Core project information including architecture, components, and features.

### [02 - Build System & Configuration](02-build-system.md)
CMake build system, dependencies, configuration files, and development environment setup.

### [03 - Plugin System](03-plugin-system.md)
Plugin architecture, core and experimental plugins, and development guidelines.

### [04 - Development Workflow](04-development-workflow.md)
Code standards, testing frameworks, CI/CD, contributing process, and best practices.

### [05 - Directory Structure & Installation](05-directory-structure.md)
Complete codebase organization, installation layout, and file system structure.

### [06 - Technical Concepts](06-technical-concepts.md)
Core technical concepts including event system, cache architecture, and performance optimizations.

## Quick Reference

### Key Executables
- `traffic_server` - Main proxy server
- `traffic_ctl` - Command line management tool
- `traffic_top` - Statistics monitoring

### Important Directories
- `src/` - Source code (iocore, proxy, mgmt, tools)
- `include/` - Header files and APIs
- `plugins/` - Core and experimental plugins
- `configs/` - Default configuration files

### Build Commands
```bash
# Basic build
cmake -B build

# With presets
cmake --preset release
cmake --build build-release
```

## Navigation Guide

- **New to the project?** Start with [Project Overview](01-project-overview.md) and [Build System](02-build-system.md)
- **Developing plugins?** See [Plugin System](03-plugin-system.md) and [Technical Concepts](06-technical-concepts.md)
- **Contributing code?** Review [Development Workflow](04-development-workflow.md) and [Directory Structure](05-directory-structure.md)
- **Understanding architecture?** Focus on [Project Overview](01-project-overview.md) and [Technical Concepts](06-technical-concepts.md)
