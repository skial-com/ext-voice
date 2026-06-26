// ============================================================================
// symbol_resolve.cpp - parse ELF .symtab to find local symbols.
// See symbol_resolve.h for rationale.
// ============================================================================

#include "symbol_resolve.h"

#include <cstring>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <cerrno>
#include <dlfcn.h>
#include <link.h>
#include <elf.h>
#include <limits.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace {

struct FindCtx {
    const char *match_basename;
    char       *out_path;
    size_t      out_path_sz;
    uintptr_t   base;
};

static int FindCB(struct dl_phdr_info *info, size_t /*size*/, void *data)
{
    auto *c = static_cast<FindCtx *>(data);
    if (!info->dlpi_name || !*info->dlpi_name) return 0;
    const char *slash = strrchr(info->dlpi_name, '/');
    const char *bn = slash ? slash + 1 : info->dlpi_name;
    if (strcmp(bn, c->match_basename) != 0) return 0;
    c->base = info->dlpi_addr;
    if (c->out_path && c->out_path_sz)
        snprintf(c->out_path, c->out_path_sz, "%s", info->dlpi_name);
    return 1;  // stop iteration
}

static uintptr_t FindLoadedLibrary(const char *basename,
                                   char *out_path, size_t out_path_sz)
{
    FindCtx ctx{basename, out_path, out_path_sz, 0};
    dl_iterate_phdr(FindCB, &ctx);
    return ctx.base;
}

}  // anonymous namespace

static void DumpLoadedLibsOnce()
{
    static bool dumped = false;
    if (dumped) return;
    dumped = true;
    fprintf(stderr, "[symbol_resolve] loaded libs snapshot:\n");
    dl_iterate_phdr([](struct dl_phdr_info *info, size_t, void *) -> int {
        fprintf(stderr, "  base=%p  name='%s'\n",
                (void *)info->dlpi_addr,
                info->dlpi_name ? info->dlpi_name : "(null)");
        return 0;
    }, nullptr);
}

void *ResolveSymtabSymbol(const char *lib_basename, const char *mangled)
{
    if (!lib_basename || !mangled) return nullptr;

    char path[PATH_MAX];
    uintptr_t base = FindLoadedLibrary(lib_basename, path, sizeof(path));
    if (!base) {
        fprintf(stderr, "[symbol_resolve] lib '%s' not found in dl_iterate_phdr\n",
                lib_basename);
        DumpLoadedLibsOnce();
        return nullptr;
    }

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "[symbol_resolve] open(%s) failed: %s\n",
                path, strerror(errno));
        return nullptr;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) { close(fd); return nullptr; }

    void *map = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED) {
        fprintf(stderr, "[symbol_resolve] mmap(%s) failed: %s\n",
                path, strerror(errno));
        return nullptr;
    }

    const auto *ehdr = static_cast<const ElfW(Ehdr) *>(map);
    const auto *shdrs = reinterpret_cast<const ElfW(Shdr) *>(
        static_cast<const char *>(map) + ehdr->e_shoff);

    // Prefer .symtab (has local symbols); fall back to .dynsym if stripped.
    const ElfW(Shdr) *symtab = nullptr, *strtab = nullptr;
    for (unsigned i = 0; i < ehdr->e_shnum; ++i) {
        if (shdrs[i].sh_type == SHT_SYMTAB) {
            symtab = &shdrs[i];
            strtab = &shdrs[shdrs[i].sh_link];
            break;
        }
    }
    if (!symtab) {
        for (unsigned i = 0; i < ehdr->e_shnum; ++i) {
            if (shdrs[i].sh_type == SHT_DYNSYM) {
                symtab = &shdrs[i];
                strtab = &shdrs[shdrs[i].sh_link];
                break;
            }
        }
    }

    void *result = nullptr;
    if (symtab && strtab) {
        const auto *syms = reinterpret_cast<const ElfW(Sym) *>(
            static_cast<const char *>(map) + symtab->sh_offset);
        const char *strs = static_cast<const char *>(map) + strtab->sh_offset;
        size_t n = symtab->sh_size / sizeof(ElfW(Sym));
        for (size_t i = 0; i < n; ++i) {
            if (syms[i].st_value == 0) continue;
            if (strcmp(strs + syms[i].st_name, mangled) == 0) {
                result = reinterpret_cast<void *>(base + syms[i].st_value);
                break;
            }
        }

        // Debug: if exact match failed, dump a few near-matches so the user
        // can see how the actual mangled name differs.
        if (!result) {
            fprintf(stderr,
                "[symbol_resolve] '%s' not found in %s (symtab size=%zu). "
                "Showing near-matches:\n", mangled, path, n);
            // Heuristic: share the first ~12 mangled chars (e.g. "_ZN8CForward")
            size_t prefix = 0;
            while (mangled[prefix] && prefix < 12) ++prefix;
            int shown = 0;
            for (size_t i = 0; i < n && shown < 10; ++i) {
                const char *nm = strs + syms[i].st_name;
                if (strncmp(nm, mangled, prefix) == 0) {
                    fprintf(stderr, "    %s\n", nm);
                    ++shown;
                }
            }
            if (shown == 0) {
                fprintf(stderr, "    (no symbols sharing prefix)\n");
            }
        }
    } else {
        fprintf(stderr,
            "[symbol_resolve] %s has neither .symtab nor .dynsym (stripped?)\n",
            path);
    }

    munmap(map, st.st_size);
    return result;
}
