#pragma once
#include "elf_file.h"
#include <m-dict.h>

#ifdef __cplusplus
extern "C" {
#endif

DICT_DEF2(AddressCache, int, M_DEFAULT_OPLIST, Elf32_Addr, M_DEFAULT_OPLIST) //-V1048

/**
 * Callable elf entry type
 */
typedef int32_t(entry_t)(void*);

typedef struct ELFSection ELFSection;

struct ELFSection {
    void* data;
    Elf32_Word size;

    size_t rel_count;
    Elf32_Off rel_offset;
    ELFSection* fast_rel;

    uint16_t sec_idx;
};

DICT_DEF2(ELFSectionDict, const char*, M_CSTR_OPLIST, ELFSection, M_POD_OPLIST)

struct ELFFile {
    size_t sections_count;
    off_t section_table;
    off_t section_table_strings;

    /* The section headers and their names, read whole when the app is opened (when small, as
     * nearly all are), so that finding a section reads no more from the card; freed with the
     * file, once the sections are loaded. NULL: read from the file */
    Elf32_Shdr* section_headers;
    char* section_names;
    size_t section_names_size;

    size_t symbol_count;
    size_t symbol_first_global; /**< .symtab's locals come first: this many */
    off_t symbol_table;
    off_t symbol_table_strings;
    off_t entry;
    ELFSectionDict_t sections;

    AddressCache_t relocation_cache;
    AddressCache_t trampoline_cache;

    File* fd;
    const ElfApiInterface* api_interface;
    ELFDebugLinkInfo debug_link_info;

    ELFSection* preinit_array;
    ELFSection* init_array;
    ELFSection* fini_array;

    bool init_array_called;
};

#ifdef __cplusplus
}
#endif