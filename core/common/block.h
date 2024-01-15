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

#ifndef CORE_COMMON_BLOCK_H_
#define CORE_COMMON_BLOCK_H_

#include <stdint.h>
#include <sys/types.h>
#include <iostream>

class Block {
public:
    inline uint64_t vaddr() { return mVaddr; }
    inline uint32_t flags() { return mFlags; }
    inline uint64_t size() { return mMemSize; }
    inline uint64_t realSize() { return mFileSize; }
    inline uint64_t align() { return mAlign; }
    inline bool isValidBlock() { return mFileSize > 0; }
    inline uint64_t oraddr() { return mOriAddr; }

    Block(uint32_t f, uint64_t off, uint64_t va, uint64_t pa,
            uint64_t filesz, uint64_t memsz, uint64_t align)
            : mFlags(f), mOffset(off), mVaddr(va), mPaddr(pa),
              mFileSize(filesz), mMemSize(memsz), mAlign(align) {}

    ~Block() {}
    void setOriAddr(uint64_t addr) { if (isValidBlock()) mOriAddr = addr; }
private:
    //  program member
    uint32_t mFlags;
    uint64_t mOffset;
    uint64_t mVaddr;
    uint64_t mPaddr;
    uint64_t mFileSize;
    uint64_t mMemSize;
    uint64_t mAlign;

    // Real memory addr
    uint64_t mOriAddr;
};

#endif  // CORE_COMMON_BLOCK_H_
