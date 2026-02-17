---
applyTo:
  - "plugins/header_rewrite/**/*"
  - "tools/hrw4u/**/*"
---

# Header Rewrite Plugin and HRW4U Transpiler

## Overview

Two closely related components that must be kept in sync:

1. **header_rewrite plugin** (`plugins/header_rewrite/`) - ATS plugin for modifying HTTP headers
2. **hrw4u transpiler** (`tools/hrw4u/`) - DSL compiler for generating header_rewrite configurations

## Critical Requirement: Feature Synchronization

**Features added to either component may require corresponding changes in the other.**

### When to Update Both

- **New operator in header_rewrite** → Add syntax and code generation in hrw4u
- **New condition in header_rewrite** → Add parsing and symbols in hrw4u
- **New variable/resource in header_rewrite** → Update hrw4u symbol tables and types
- **New hook in header_rewrite** → Add hook syntax in hrw4u
- **New hrw4u syntax** → Ensure correct header_rewrite config generation

### Bidirectional Compilation

Both directions must work:
- **hrw4u** (forward): HRW4U source → header_rewrite config
- **u4wrh** (reverse): header_rewrite config → HRW4U source

Round-trip test: `hrw4u example.hrw4u | u4wrh` should produce equivalent output.

## Header Rewrite Plugin

### Architecture

**Core files:**
- `parser.cc/h` - Configuration syntax parser
- `factory.cc/h` - Factory for operators and conditions
- `operators.cc/h` - Header manipulation operations
- `conditions.cc/h` - Conditional logic
- `resources.cc/h` - Available variables (headers, IPs, etc.)
- `statement.cc/h` - Rule statement abstraction
- `ruleset.cc/h` - Rule collection and execution
- `matcher.cc/h` - Pattern matching
- `value.cc/h` - Value extraction and manipulation

### Adding Features

**New operator:**
1. Define class in `operators.h`, implement in `operators.cc`
2. Register in `factory.cc`
3. Update hrw4u: `tables.py` (forward mapping tables), `visitor.py` (forward compiler - HRW4UVisitor), and `generators.py` (reverse-resolution tables used by u4wrh)

**New condition:**
1. Define class in `conditions.h`, implement in `conditions.cc`
2. Register in `factory.cc`
3. Update hrw4u: `visitor.py` for parsing, `tables.py` for symbol maps

**New resource/variable:**
1. Define in `resources.h`, implement in `resources.cc`
2. Update hrw4u: `types.py` for type system, `tables.py` (OPERATOR_MAP/CONDITION_MAP/etc.) for symbol tables, `symbols.py` for resolver wiring, and `generators.py` for reverse mappings

## HRW4U Transpiler

### Purpose

Provides readable DSL syntax that compiles to header_rewrite configuration.

**Requirements:** Python 3.11+, ANTLR4

### Project Structure

```
tools/hrw4u/
├── src/                    # Python source
│   ├── common.py          # Shared utilities
│   ├── types.py           # Type system
│   ├── symbols.py         # Symbol resolution
│   ├── hrw_symbols.py     # Header rewrite symbols
│   ├── tables.py          # Symbol/type tables
│   ├── visitor.py         # Forward compiler (HRW4UVisitor - hrw4u script)
│   ├── hrw_visitor.py     # Reverse compiler (HRWInverseVisitor - u4wrh script)
│   ├── generators.py      # Reverse-resolution table generation
│   ├── validation.py      # Semantic validation
│   └── lsp/               # LSP server
├── scripts/               # CLI tools
│   ├── hrw4u             # Forward compiler (hrw4u → HRW config)
│   ├── u4wrh             # Reverse compiler (HRW config → hrw4u)
│   └── hrw4u-lsp         # LSP server
├── grammar/              # ANTLR4 grammars
└── tests/                # Test suite
```

### Key Modules

**Type System (`types.py`):**
- HRW4U type hierarchy
- Variable types (string, int, bool, IP, etc.)
- Operator signatures
- Type checking and inference

**Symbol Resolution (`symbols.py`, `hrw_symbols.py`, `tables.py`):**
- Symbol tables for variables, operators, functions
- Scope management
- Built-in symbols for header_rewrite resources

**Reverse-Resolution Tables (`generators.py`):**
- Generates derived tables and reverse mappings from primary forward tables
- Used by u4wrh (reverse compiler) to map HRW config back to hrw4u syntax
- Eliminates duplication by maintaining single source of truth in forward tables

**Visitors:**
- `visitor.py` (HRW4UVisitor) - Forward compilation: hrw4u DSL → header_rewrite config
- `hrw_visitor.py` (HRWInverseVisitor) - Reverse compilation: header_rewrite config → hrw4u DSL
- `kg_visitor.py` (KnowledgeGraphVisitor) - Extracts structured graph data for analysis/visualization (used by `hrw4u-kg` script, rarely modified)

### Adding Features

**New operator:**
1. Update grammar if new syntax needed
2. Add symbol definition in `hrw_symbols.py`
3. Add type signature in `types.py`
4. Update forward compiler in `visitor.py` (HRW4UVisitor) to handle new operator
5. Update `generators.py` to generate reverse mappings for u4wrh
6. Update reverse compiler in `hrw_visitor.py` (HRWInverseVisitor) if special handling needed
7. Add tests in `tests/test_ops.py` and `tests/test_ops_reverse.py`
8. Update corresponding header_rewrite plugin code

**New condition:**
1. Update grammar if needed
2. Add symbol definition in `hrw_symbols.py` and type info in `types.py`
3. Update forward compiler in `visitor.py` (HRW4UVisitor)
4. Update `generators.py` for reverse mappings
5. Update reverse compiler in `hrw_visitor.py` (HRWInverseVisitor) if needed
6. Add tests
7. Update header_rewrite plugin

**New variable:**
1. Add to symbol tables (`tables.py`, `hrw_symbols.py`)
2. Add type definition (`types.py`)
3. Update forward compiler in `visitor.py` (HRW4UVisitor) for property access
4. Update `generators.py` for reverse mappings
5. Add tests
6. Ensure header_rewrite supports it

### Code Style

**Python (3.11+):**
- 4-space indentation (never tabs)
- Type hints on all functions
- Dataclasses for structured data
- Modern Python features (match/case, walrus operator)

**C++ (header_rewrite):**
- Follow ATS C++20 standards
- CamelCase classes, snake_case functions/variables
- 2-space indentation
- Empty line after declarations

## Feature Addition Example

**Hypothetical example to illustrate the workflow:**

Adding a `has-prefix` operator (this operator does not exist):

1. **header_rewrite plugin:**
   ```cpp
   // operators.h
   class OperatorHasPrefix : public Operator {
     void exec(const Resources &res) override;
   };

   // operators.cc - implement exec()
   // factory.cc - register operator
   ```

2. **hrw4u transpiler:**
   ```python
   # hrw_symbols.py
   OPERATORS = {
       'has-prefix': OperatorSymbol(
           name='has-prefix',
           params=['target', 'prefix'],
           return_type=BoolType()
       ),
   }

   # generators.py
   def generate_has_prefix_op(target, prefix):
       return f'has-prefix {target} {prefix}'

   # tests/test_ops.py
   def test_has_prefix():
       # Test forward compilation

   # tests/test_ops_reverse.py
   def test_has_prefix_reverse():
       # Test reverse compilation
   ```

3. **Verify round-trip:**
   ```bash
   echo 'REMAP { if req.Host has-prefix "www." { } }' | hrw4u | u4wrh
   ```

## Common Pitfalls

1. **Forgetting to update both components** - Changes often need coordination
2. **Breaking round-trip** - Always test `hrw4u | u4wrh` round-trip
3. **Symbol table drift** - Keep hrw4u symbols synced with plugin capabilities
4. **Type mismatches** - Ensure type system matches plugin runtime behavior
5. **Missing tests** - Add tests for both forward and reverse compilation

## Documentation

- User docs: `doc/admin-guide/plugins/header_rewrite.en.html`
- Plugin README: `plugins/header_rewrite/README`
- HRW4U README: `tools/hrw4u/README.md`
- LSP README: `tools/hrw4u/LSP_README.md`
