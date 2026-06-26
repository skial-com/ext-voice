// ============================================================================
// symbol_resolve.h
//
// Find symbols by mangled name, including *local* ones (lowercase 't'/'d' in
// `nm`). SourceMod's memutils / dlsym only see .dynsym (global exports), so
// local symbols like CForward::Execute are invisible to them. These helpers
// parse the on-disk ELF's .symtab directly.
//
// Linux only (uses dl_iterate_phdr + ELF32/64). Safe to call at any time after
// the target library is loaded.
// ============================================================================
#pragma once

// Look up a mangled symbol in the .symtab (falls back to .dynsym if stripped)
// of a loaded shared library identified by basename (e.g. "sourcemod.logic.so").
// Returns the runtime address, or nullptr if the library isn't loaded or the
// symbol isn't found.
void *ResolveSymtabSymbol(const char *lib_basename, const char *mangled);
