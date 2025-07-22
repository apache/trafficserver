# Apache Traffic Server - Development Workflow

## Development Workflow

### Code Standards
- **C++20** standard compliance
- **clang-format** for code formatting
- **yapf** for Python code formatting
- **cmake-format** for CMake files

### Testing
- **Regression tests**: `BUILD_REGRESSION_TESTING=ON`
- **AuTest**: Automated testing framework
- **Fuzzing**: Clang fuzzer support
- **CI/CD**: Jenkins-based continuous integration

### Contributing Process
1. Fork and branch from `master`
2. Create GitHub Pull Request
3. Ensure CI passes (Jenkins builds)
4. Code review and approval required
5. Merge by committers only

### Code Quality Tools
- **clang-analyzer**: Static analysis
- **clang-tidy**: Linting and modernization
- **ThreadSanitizer**: Thread safety analysis
- **AddressSanitizer**: Memory error detection
- **Coverage analysis**: Code coverage reporting

### Development Best Practices
- Follow existing code patterns and conventions
- Write comprehensive tests for new features
- Document public APIs and complex algorithms
- Use modern C++ features appropriately
- Consider performance implications of changes

### Debugging and Profiling
- **traffic_crashlog**: Crash analysis
- **traffic_top**: Real-time statistics monitoring
- **traffic_logstats**: Log analysis and metrics
- Built-in debugging hooks and statistics
