# HRW4U Language Server Protocol (LSP) Support

Language Server Protocol implementation for HRW4U, providing IDE/editor support for Apache Traffic Server header rewrite configuration files.

## Features

- **Real-time Diagnostics** - Syntax and semantic error checking
- **Auto-completion** - Context-aware suggestions for keywords, operators, and functions
- **Hover Information** - Documentation on hover for language constructs
- **Multi-error Detection** - Comprehensive error reporting in a single pass

## LSP Capabilities

- **Document Synchronization** - Real-time document state management
- **Diagnostics** - Error reporting with precise line/column positions
- **Completion** - Context-aware completions:
  - Section types (READ_REQUEST, REMAP, etc.)
  - Condition operators (inbound.req.host, outbound.status, etc.)
  - Operation targets (inbound.resp.body, outbound.cookie.*, etc.)
  - Functions (access, cache, random, etc.)
- **Hover** - Basic hover information

## Usage

The LSP server is available as the `hrw4u-lsp` command after installation.

### VS Code Integration
Use the `vscode-hrw4u` extension for seamless VS Code support.

### Other Editors
The LSP server follows the standard LSP protocol and works with:
- **Neovim** - Using built-in LSP client or `nvim-lspconfig`
- **Emacs** - Using `lsp-mode` or `eglot`
- **Vim** - Using `vim-lsp` or `coc.nvim`
- **Sublime Text** - Using LSP plugin
- **Any LSP-compatible editor**

## Example Features

### Auto-completion
- Type `READ_` → suggests `READ_REQUEST`, `READ_RESPONSE`
- Type `inbound.` → suggests `inbound.req.`, `inbound.resp.`, `inbound.cookie.`, etc.
- Type `outbound.req.` → suggests available header operations

### Real-time Diagnostics
```hrw4u
READ_REQUEST {
  inbound.req.host = "example.com"  // ✓ Valid
  invalid.syntax = "test"           // ❌ Error: Unknown operator 'invalid.syntax'
  inbound.req.missing_value =       // ❌ Error: Missing value
}
```

## Configuration

Most editors can be configured to use `hrw4u-lsp` automatically. For manual configuration, the server:
- Reads JSON-RPC messages from stdin
- Writes responses to stdout
- Supports standard LSP initialization and capabilities exchange
