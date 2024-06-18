/*
 * Copyright (C) 2024-present, Guanyou.Chen. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "logger/log.h"
#include "lp64/core.h"
#include "api/memory_ref.h"
#include "zip/zip_file.h"
#include "common/elf.h"
#include "common/bit.h"
#include "common/load_block.h"
#include "common/exception.h"
#include <string.h>
#include <linux/elf.h>

bool lp64::Core::load64(CoreApi* api, std::function<void* (uint64_t, uint64_t)> callback) {
    Elf64_Ehdr *ehdr = reinterpret_cast<Elf64_Ehdr *>(api->begin());
    Elf64_Phdr *phdr = reinterpret_cast<Elf64_Phdr *>(api->begin() + ehdr->e_phoff);

    for (int num = 0; num < ehdr->e_phnum; ++num) {
        if (phdr[num].p_type == PT_LOAD) {
            std::shared_ptr<LoadBlock> block(new LoadBlock(phdr[num].p_flags,
                                             phdr[num].p_offset,
                                             phdr[num].p_vaddr,
                                             phdr[num].p_paddr,
                                             phdr[num].p_filesz,
                                             phdr[num].p_memsz,
                                             phdr[num].p_align));
            block->setTruncated(api->size() < (phdr[num].p_offset + phdr[num].p_filesz));
            if (!(block->flags() & Block::FLAG_R))
                continue;

            block->setOriAddr(api->begin() + phdr[num].p_offset);
            block->setVabitsMask(CoreApi::GetVabitsMask());
            block->setPointMask(CoreApi::GetPointMask());
            api->addLoadBlock(block);
        } else if (phdr[num].p_type == PT_NOTE) {
            std::unique_ptr<NoteBlock> block(new NoteBlock(phdr[num].p_flags,
                                             phdr[num].p_offset,
                                             phdr[num].p_vaddr,
                                             phdr[num].p_paddr,
                                             phdr[num].p_filesz,
                                             phdr[num].p_memsz,
                                             phdr[num].p_align));
            block->setTruncated(api->size() < (phdr[num].p_offset + phdr[num].p_filesz));
            if (!block->isValidBlock())
                continue;

            block->setOriAddr(api->begin() + phdr[num].p_offset);
            uint64_t pos = block->oraddr();
            uint64_t end = block->oraddr() + block->realSize();
            while (pos < end) {
                Elf64_Nhdr *nhdr = reinterpret_cast<Elf64_Nhdr *>(pos);
                uint64_t item_pos = nhdr->n_descsz == 0 ? 0 : pos + sizeof(Elf64_Nhdr) + RoundUp(nhdr->n_namesz, 0x4);
                switch(nhdr->n_type) {
                    case NT_PRSTATUS:
                        block->addThreadItem(callback(NT_PRSTATUS, item_pos));
                        break;
                    case NT_AUXV: {
                        int numauxv = nhdr->n_descsz / sizeof(lp64::Auxv);
                        lp64::Auxv* auxv = reinterpret_cast<lp64::Auxv *>(item_pos);
                        for (int index = 0; index < numauxv; ++index) {
                            block->addAuxvItem(auxv[index].type, auxv[index].value);
                        }
                    } break;
                    case NT_FILE: {
                        uint64_t numfile = reinterpret_cast<uint64_t *>(item_pos)[0];
                        uint64_t page_size = reinterpret_cast<uint64_t *>(item_pos)[1];
                        lp64::File *file = reinterpret_cast<lp64::File *>(item_pos
                                                        + sizeof(uint64_t) // NUMBER
                                                        + sizeof(uint64_t)); // PAGE_SIZE
                        uint64_t name_base = reinterpret_cast<uint64_t>(file) + sizeof(lp64::File) * numfile;
                        int index = 0;
                        while (index < numfile) {
                            block->addFileItem(file[index].begin, file[index].end, file[index].offset, name_base);
                            name_base += strlen(reinterpret_cast<const char *>(name_base)) + 1;
                            ++index;
                        }
                    } break;
                }

                pos = pos + sizeof(Elf64_Nhdr) + RoundUp(nhdr->n_namesz, 0x4) + RoundUp(nhdr->n_descsz, 0x4);
            }

            api->addNoteBlock(block);
        }
    }
    LOGI("Core load (%p) %s\n", this, api->getName().c_str());
    return true;
}

void lp64::Core::loadLinkMap64(CoreApi* api) {
    api::MemoryRef& debug = GetDebug();
    if (!debug.Ptr()) {
        LOGW("WARN: Not found debug. You can try command exec.\n");
        return;
    }

    try {
        lp64::Debug* dbg = reinterpret_cast<lp64::Debug*>(debug.Real());
        uint64_t map = dbg->map;
        while (map) {
            lp64::LinkMap* link = reinterpret_cast<lp64::LinkMap*>(CoreApi::GetReal(map));
            api->addLinkMap(map);
            map = link->next;
        }
    } catch (InvalidAddressException e) {
        LOGI("%s\n", e.what());
    }
}

bool lp64::Core::exec64(CoreApi* api, uint64_t phdr, const char* file) {
    std::unique_ptr<MemoryMap> map(MemoryMap::MmapFile(file));
    if (map) {
        ElfHeader* header = reinterpret_cast<ElfHeader*>(map->data());
        if (memcmp(header->ident, ELFMAG, 4)) {
            LOGE("ERROR: Invalid ELF file (%s)\n", file);
            return false;
        }

        if (header->type != ET_DYN) {
            LOGE("ERROR: Invalid shared object file (%s)\n", file);
            return false;
        }

        if (header->machine != CoreApi::GetMachine()) {
            LOGE("ERROR: Invalid match machine(%d) (%s)\n", header->machine, file);
            return false;
        }

        Elf64_Ehdr* ehdr = reinterpret_cast<Elf64_Ehdr*>(map->data());
        Elf64_Phdr* pt = reinterpret_cast<Elf64_Phdr*>(map->data() + ehdr->e_phoff);

        if (pt[0].p_type != PT_PHDR) {
            LOGE("ERROR: Exec file PHDR segment non-first\n");
            return false;
        }

        for (int index = 0; index < ehdr->e_phnum; ++index) {
            if (pt[index].p_type != PT_LOAD)
                continue;

            if (pt[index].p_flags & PF_W)
                continue;

            uint64_t current = phdr - ehdr->e_phoff -
                               (pt[0].p_vaddr - pt[0].p_offset) +
                               RoundDown(pt[index].p_vaddr, pt[index].p_align);
            LoadBlock* block = api->findLoadBlock(current);

            if (!block) {
                LOGE("ERROR: Not found LoadBlock(%lx)\n", current);
                continue;
            }

            if (block->flags() & PF_W)
                continue;

            if (current != block->vaddr())
                continue;

            uint64_t page_offset = RoundDown(pt[index].p_offset + map->offset(), 0x1000);
            ::File* vma = CoreApi::FindFile(current);
            if (vma && page_offset != vma->offset()) {
                page_offset = vma->offset();
            }
            block->setMmapFile(file, page_offset);
        }
        return true;
    }
    return false;
}

bool lp64::Core::dlopen64(CoreApi* api, ::LinkMap* handle, const char* file, const char* subfile) {
    std::unique_ptr<MemoryMap> map(MemoryMap::MmapFile(file));
    if (subfile) {
        ZipFile zip;
        if (zip.open(file)) {
            LOGE("ERROR: Zip open fail %s\n", file);
            return false;
        }

        ZipEntry* entry;
        if (subfile[0] == '/') {
            entry = zip.getEntryByName(subfile + 1);
        } else {
            entry = zip.getEntryByName(subfile);
        }
        if (!entry) {
            LOGE("ERROR: %s Not found entry %s\n", file, subfile);
            return false;
        }

        if (!entry->IsUncompressed()) {
            LOGE("ERROR: Not support compress zip %s!%s\n", file, entry->getFileName());
            return false;
        }

        std::unique_ptr<MemoryMap> submap(MemoryMap::MmapFile(file,
                                                              RoundUp(entry->getEntryTotalMemsz(), 0x1000),
                                                              entry->getFileOffset()));
        map = std::move(submap);
    }
    if (map) {
        ElfHeader* header = reinterpret_cast<ElfHeader*>(map->data());
        if (memcmp(header->ident, ELFMAG, 4)) {
            LOGE("ERROR: Invalid ELF file (%s)\n", file);
            return false;
        }

        if (header->type != ET_DYN) {
            LOGE("ERROR: Invalid shared object file (%s)\n", file);
            return false;
        }

        if (header->machine != CoreApi::GetMachine()) {
            LOGE("ERROR: Invalid match machine(%d) (%s)\n", header->machine, file);
            return false;
        }

        Elf64_Ehdr* ehdr = reinterpret_cast<Elf64_Ehdr*>(map->data());
        Elf64_Phdr* phdr = reinterpret_cast<Elf64_Phdr*>(map->data() + ehdr->e_phoff);

        for (int index = 0; index < ehdr->e_phnum; ++index) {
            if (phdr[index].p_type != PT_LOAD)
                continue;

            if (phdr[index].p_flags & PF_W)
                continue;

            uint64_t current = handle->l_addr() + RoundDown(phdr[index].p_vaddr, phdr[index].p_align);
            LoadBlock* block = api->findLoadBlock(current);

            if (!block) {
                LOGE("ERROR: Not found LoadBlock(%lx)\n", current);
                continue;
            }

            if (block->flags() & PF_W)
                continue;

            if (current != block->vaddr())
                continue;

            uint64_t page_offset = RoundDown(phdr[index].p_offset + map->offset(), 0x1000);
            ::File* vma = CoreApi::FindFile(current);
            if (vma && page_offset != vma->offset()) {
                page_offset = vma->offset();
            }
            block->setMmapFile(file, page_offset);
        }
        return true;
    }
    return false;
}

uint64_t lp64::Core::dlsym64(const char* path, const char* symbol) {
    std::unique_ptr<MemoryMap> map(MemoryMap::MmapFile(path));
    if (map) {
        // already check valid on dlopen
        Elf64_Ehdr* ehdr = reinterpret_cast<Elf64_Ehdr*>(map->data());
        Elf64_Phdr* phdr = reinterpret_cast<Elf64_Phdr*>(map->data() + ehdr->e_phoff);

        int dynsymndx = -1;
        int dynstrndx = -1;
        int symtabndx = -1;
        int strtabndx = -1;
        int gnu_debugdatandx = -1;

        int sh_num = ehdr->e_shnum;
        Elf64_Shdr* shdr = reinterpret_cast<Elf64_Shdr*>(map->data() + ehdr->e_shoff);
        const char* shstr = reinterpret_cast<const char*>(map->data() + shdr[ehdr->e_shstrndx].sh_offset);
        for (int i = 0; i < sh_num; ++i) {
            if (!strcmp(shstr + shdr[i].sh_name, ".dynsym")) {
                dynsymndx = i;
                continue;
            }

            if (!strcmp(shstr + shdr[i].sh_name, ".dynstr")) {
                dynstrndx = i;
                continue;
            }

            if (!strcmp(shstr + shdr[i].sh_name, ".symtab")) {
                symtabndx = i;
                continue;
            }

            if (!strcmp(shstr + shdr[i].sh_name, ".strtab")) {
                strtabndx = i;
                continue;
            }

            if (!strcmp(shstr + shdr[i].sh_name, ".gnu_debugdata")) {
                gnu_debugdatandx = i;
                continue;
            }
        }

        // scan dynsym
        if (dynsymndx > 0 && dynstrndx > 0) {
            int count = shdr[dynsymndx].sh_size / shdr[dynsymndx].sh_entsize;
            Elf64_Sym* symtab = reinterpret_cast<Elf64_Sym*>(map->data() + shdr[dynsymndx].sh_offset);
            const char* strtab = reinterpret_cast<const char*>(map->data() + shdr[dynstrndx].sh_offset);
            for (int i = 0; i < count; ++i) {
                if (!strcmp(strtab + symtab[i].st_name, symbol))
                    return symtab[i].st_value;
            }
        }

        // scan symtab
        if (symtabndx > 0 && strtabndx > 0) {
            int count = shdr[symtabndx].sh_size / shdr[symtabndx].sh_entsize;
            Elf64_Sym* symtab = reinterpret_cast<Elf64_Sym*>(map->data() + shdr[symtabndx].sh_offset);
            const char* strtab = reinterpret_cast<const char*>(map->data() + shdr[strtabndx].sh_offset);
            for (int i = 0; i < count; ++i) {
                if (!strcmp(strtab + symtab[i].st_name, symbol))
                    return symtab[i].st_value;
            }
        }

        // scan gnu_debugdata
    }
    return 0x0;
}
