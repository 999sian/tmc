---
name: code-change-signature-hygiene
description: Use when making C or C++ code changes in this repository. Enforces that edited functions keep return statements consistent with their declared return types, and that any new function added in a change also has the required declaration and definition so no new symbols are left missing or mismatched.
---

# Code Change Signature Hygiene

Apply this skill on any code-editing task in this repository that adds or modifies C or C++ functions.

## Required checks

Before finishing a code change:

1. Verify each edited function returns values compatible with its declared return type.
2. Verify `void` functions do not return a value.
3. Verify non-`void` functions do not fall through without returning a value on reachable paths.
4. If you add a new function, ensure it has the declarations the codebase expects.
5. If you add a new non-`static` function, ensure the prototype is present in the appropriate header or shared declaration site.
6. If you add a new `static` function, ensure it has a declaration in the same file if local style expects one before use.
7. Ensure no callsites or forward declarations use a mismatched signature.

## Repository-specific workflow

When you add or change functions:

- Read the local file header/prototype section before editing.
- Keep new prototypes grouped with existing forward declarations.
- Prefer matching existing calling conventions and typedef usage exactly.
- After editing, inspect the diff specifically for:
  - changed return types
  - new helper functions
  - forward declarations
  - header declarations
- If a new function was introduced only to support one file, prefer `static` unless cross-file use is required.

## Final pass

Before returning to the user, explicitly check:

- return statements versus function signatures
- new functions for both declaration and definition presence
- no newly introduced missing prototypes
- no declaration/definition signature drift

If verification was not run with a compiler or tests, say so in the final response.
