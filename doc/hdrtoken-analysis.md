# HdrToken System Analysis

This document describes the purpose and inner workings of the `HdrToken` system in Traffic Server, including the algorithms used for initialization and runtime parsing.

## Purpose

The `HdrToken` system is an optimization designed to efficiently handle frequently occurring strings in HTTP headers, such as header names (e.g., "Content-Type") and common values (e.g., "chunked").

Instead of repeatedly comparing strings, the system maps these "well-known strings" (WKS) to unique integer identifiers and stores them in a special memory heap. At runtime, incoming header strings can be quickly checked against this optimized data structure. If a match is found, the system can use the faster integer representation and canonical string pointer for internal logic instead of performing more expensive string operations.

## Algorithms and Data Structures

The `HdrToken` system uses different algorithms for its one-time initialization phase and its runtime parsing phase.

### Initialization (`hdrtoken_init`)

During server startup, the `hdrtoken_init` function is called once to set up the necessary data structures for runtime use.

1.  **Well-Known Strings Lists**: Several static, compile-time arrays define the strings and their metadata:
    *   `_hdrtoken_strs`: The master list of all well-known strings.
    *   `_hdrtoken_strs_type_initializers`: Associates certain strings with a `HdrTokenType` (e.g., "GET" is a METHOD).
    *   `_hdrtoken_strs_field_initializers`: Associates header field names with slot IDs, presence masks, and flags.

2.  **DFA (Deterministic Finite Automaton)**:
    *   **Algorithm**: A DFA is compiled from the master `_hdrtoken_strs` list.
    *   **Purpose**: The DFA's sole purpose is to act as a temporary lookup mechanism during the initialization phase. It provides a robust and consistent way to find the canonical index of a string from the metadata arrays (`_hdrtoken_strs_type_initializers`, etc.) within the master `_hdrtoken_strs` array. This is crucial for correctly linking metadata to the right string before the primary runtime data structures are built. For example, it's used to find the index of "GET" so its `HdrTokenType` can be set to `METHOD`. The DFA is configured to be case-insensitive. After initialization is complete, the DFA is **not** used for parsing headers at runtime.

3.  **Specialized Memory Heap**:
    *   All well-known strings from the master list are copied into a single contiguous block of memory (a dedicated heap). Each string in the heap is prefixed with a `HdrTokenHeapPrefix` struct that stores its length, index, and other metadata.
    *   This allows for a very fast runtime check: if a string's pointer falls within the address range of this heap, it is instantly identifiable as a well-known string without needing a hash or comparison.

4.  **Hash Table (`hdrtoken_hash_table`)**:
    *   **Algorithm**: FNV-1a (Fowler–Noll–Vo) hash.
    *   **Purpose**: This is the primary data structure for fast lookups of unknown strings at runtime. After the heap is populated, the `hdrtoken_hash_init` function iterates through the well-known strings, calculates the FNV-1a hash for each one, and stores the canonical string pointer (from the heap) and its hash in the `hdrtoken_hash_table`.

### Runtime Parsing and Tokenization

When the server is running, headers are parsed and strings are "tokenized" (converted to a WKS representation) on the fly. This process is handled by the `hdrtoken_tokenize` function.

1.  **Fast Path (Pointer Check)**: The function first checks if the string pointer `hdrtoken_is_wks()` is already a known token by seeing if its address falls within the specialized memory heap. If it is, the string has already been tokenized, and the function immediately returns its index.

2.  **Slow Path (Hash Lookup)**: If the string is not already a known token pointer (i.e., it's a fresh string from the network), the following occurs:
    *   **Algorithm**: FNV-1a hash and direct memory comparison.
    *   The FNV-1a hash of the input string is calculated.
    *   This hash is used to find a potential match in the `hdrtoken_hash_table`.
    *   A three-part check is performed to validate the match:
        1.  The hash bucket must contain a valid string (`bucket->wks != nullptr`).
        2.  The hash of the string in the bucket must match the hash of the input string (`bucket->hash == hash`).
        3.  The length of the string in the bucket must match the length of the input string (`hdrtoken_wks_to_length(bucket->wks) == string_len`).

This final length check is critical. The combination of a matching hash and matching length provides a very strong guarantee of a **whole-string match**. It prevents a shorter string like "Accept" from incorrectly matching a longer string like "Accept-Ranges".

If all checks pass, the string has been successfully "tokenized". The function returns the WKS index. Other parts of the system can then use `hdrtoken_index_to_wks(index)` to get a pointer to the canonical, shared string from the WKS heap. If the checks fail, the string is not a well-known token.

## Sequence of Steps for Parsing a Header String

1.  An unknown header string is passed to `hdrtoken_tokenize`.
2.  A quick check (`hdrtoken_is_wks`) is performed to see if the string's memory address is already in the well-known string heap. If yes, the token is found, and the process ends.
3.  If not, the string's FNV-1a hash is calculated.
4.  A bucket in the `hdrtoken_hash_table` is located using the hash.
5.  The hash stored in the bucket is compared to the input string's hash.
6.  The length of the string in the bucket is compared to the input string's length.
7.  If both the hash and the length match, the string is successfully tokenized. Its WKS index is returned for use by the caller.
8.  If any of these checks fail, the string is not a well-known token.
