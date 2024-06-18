/*
 * Copyright (C) 2024-present, Guanyou.Chen. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file ercept in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either erpress or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "api/core.h"
#include "common/elf.h"
#include "android.h"
#include "runtime/oat.h"
#include "runtime/oat_quick_method_header.h"
#include "runtime/entrypoints/runtime_asm_entrypoints.h"

struct OatQuickMethodHeader_OffsetTable __OatQuickMethodHeader_offset__;
struct OatQuickMethodHeader_SizeTable __OatQuickMethodHeader_size__;

namespace art {

uint32_t OatQuickMethodHeader::kIsCodeInfoMask = 0x40000000;
uint32_t OatQuickMethodHeader::kCodeInfoMask = 0x3FFFFFFF;
uint32_t OatQuickMethodHeader::kCodeSizeMask = 0x3FFFFFFF;

api::MemoryRef OatQuickMethodHeader::NterpMethodHeader = 0x0;
api::MemoryRef OatQuickMethodHeader::NterpWithClinitImpl = 0x0;
api::MemoryRef OatQuickMethodHeader::NterpImpl = 0x0;

void OatQuickMethodHeader::OatInit124() {
    kCodeSizeMask   = ~kShouldDeoptimizeMask;

    __OatQuickMethodHeader_offset__ = {
        .vmap_table_offset_ = 0,
        .method_info_offset_ = 4,
        .frame_info_ = 8,
        .code_size_ = 20,
        .code_ = 24,
    };

    __OatQuickMethodHeader_size__ = {
        .THIS = 24,
    };
}

void OatQuickMethodHeader::OatInit156() {
    __OatQuickMethodHeader_offset__ = {
        .vmap_table_offset_ = 0,
        .method_info_offset_ = 4,
        .code_size_ = 8,
        .code_ = 12,
    };

    __OatQuickMethodHeader_size__ = {
        .THIS = 12,
    };
}

void OatQuickMethodHeader::OatInit158() {
    __OatQuickMethodHeader_offset__ = {
        .vmap_table_offset_ = 0,
        .code_size_ = 4,
        .code_ = 8,
    };

    __OatQuickMethodHeader_size__ = {
        .THIS = 8,
    };
}

void OatQuickMethodHeader::OatInit192() {
    kIsCodeInfoMask = 0x40000000;
    kCodeInfoMask   = 0x3FFFFFFF;
    kCodeSizeMask   = 0x3FFFFFFF;

    __OatQuickMethodHeader_offset__ = {
        .data_ = 0,
        .code_ = 4,
    };

    __OatQuickMethodHeader_size__ = {
        .THIS = 4,
    };
}

void OatQuickMethodHeader::OatInit238() {
    kIsCodeInfoMask = 0x80000000;
    kCodeInfoMask   = 0x7FFFFFFF;
    kCodeSizeMask   = 0x7FFFFFFF;
}

void OatQuickMethodHeader::OatInit239() {
    __OatQuickMethodHeader_offset__ = {
        .code_info_offset_ = 0,
        .code_ = 4,
    };

    __OatQuickMethodHeader_size__ = {
        .THIS = 4,
    };
}

bool OatQuickMethodHeader::Contains(uint64_t pc) {
    uint64_t code_start = GetCodeStart();
    return code_start <= pc && pc <= (code_start + GetCodeSize());
}

bool OatQuickMethodHeader::IsOptimized() {
    if (OatHeader::OatVersion() >= 239) {
        if ((GetNterpWithClinitImpl().Ptr() && code() == GetNterpWithClinitImpl().valueOf())
                || (GetNterpImpl().Ptr() && code() == GetNterpImpl().valueOf())) {
            return false;
        }
        return true;
    } if (OatHeader::OatVersion() >= 192) {
        return (data() & kIsCodeInfoMask) != 0;
    } else {
        return GetCodeSize() != 0 && vmap_table_offset() != 0;
    }
}

uint64_t OatQuickMethodHeader::GetCodeStart() {
    uint64_t code_start = code();
    if (CoreApi::GetMachine() == EM_AARCH64) {
        code_start &= ((1ULL << 56) - 1);
    } else if (CoreApi::GetMachine() == EM_ARM) {
        code_start++;
    }
    return code_start;
}

uint32_t OatQuickMethodHeader::GetCodeSize() {
    if (OatHeader::OatVersion() >= 239) {
        if (code() == GetNterpWithClinitImpl().valueOf()) {
            return GetNterpWithClinitImpl().valueOf(CoreApi::GetPointSize());
        }
        if (code() == GetNterpImpl().valueOf()) {
            return GetNterpImpl().valueOf(CoreApi::GetPointSize());
        }
        return CodeInfo::DecodeCodeSize(GetOptimizedCodeInfoPtr());
    } else if (OatHeader::OatVersion() >= 192) {
        if (IsOptimized()) {
            return CodeInfo::DecodeCodeSize(GetOptimizedCodeInfoPtr());
        } else {
            return (data() & kCodeSizeMask);
        }
    } else {
        return code_size() & kCodeSizeMask;
    }
}

uint32_t OatQuickMethodHeader::GetCodeInfoOffset() {
    if (OatHeader::OatVersion() >= 239) {
        return code_info_offset();
    } else {
        return data() & kCodeInfoMask;
    }
}

QuickMethodFrameInfo OatQuickMethodHeader::GetFrameInfo() {
    if (OatHeader::OatVersion() >= 156) {
        return CodeInfo::DecodeFrameInfo(GetOptimizedCodeInfoPtr());
    } else {
        api::MemoryRef ref(frame_info(), this);
        return QuickMethodFrameInfo(ref.value32Of(), ref.value32Of(4), ref.value32Of(8));
    }
}

bool OatQuickMethodHeader::IsNterpPc(uint64_t pc) {
    OatQuickMethodHeader header = GetNterpMethodHeader();
    return header.Ptr() && header.Contains(pc);
}

OatQuickMethodHeader OatQuickMethodHeader::GetNterpMethodHeader() {
    if (NterpMethodHeader.Ptr())
        return NterpMethodHeader;

    uint64_t entry_point = GetExecuteNterpImplEntryPoint();
    if (entry_point) {
        OatQuickMethodHeader::FromEntryPoint(entry_point);
        NterpMethodHeader = OatQuickMethodHeader::FromEntryPoint(entry_point);
        return NterpMethodHeader;
    }

    try {
        api::MemoryRef value = Android::SearchSymbol(Android::NTERP_METHOD_HEADER);
        NterpMethodHeader = value.valueOf();
    } catch(InvalidAddressException e) {
    }

    if (!NterpMethodHeader.Ptr()) {
        if (OatHeader::OatVersion() >= 239) {
            NterpMethodHeader = GetNterpImpl().Ptr() ? FromCodePointer(GetNterpImpl().valueOf()) : 0x0;
        }
    }

    return NterpMethodHeader;
}

uint32_t OatQuickMethodHeader::NativePc2DexPc(uint32_t native_pc) {
    CodeInfo code_info = CodeInfo::Decode(GetOptimizedCodeInfoPtr());
    return code_info.NativePc2DexPc(native_pc);
}

void OatQuickMethodHeader::NativePc2VRegs(uint32_t native_pc, std::map<uint32_t, CodeInfo::DexRegisterInfo>& vregs) {
    CodeInfo code_info = CodeInfo::Decode(GetOptimizedCodeInfoPtr());
    code_info.NativePc2VRegs(native_pc, vregs);
}

api::MemoryRef& OatQuickMethodHeader::GetNterpWithClinitImpl() {
    if (!NterpWithClinitImpl.Ptr()) {
        NterpWithClinitImpl = Android::SearchSymbol(Android::NTERP_WITH_CLINT_IMPL);
    }
    return NterpWithClinitImpl;
}

api::MemoryRef& OatQuickMethodHeader::GetNterpImpl() {
    if (!NterpImpl.Ptr()) {
        NterpImpl = Android::SearchSymbol(Android::NTERP_IMPL);
    }
    return NterpImpl;
}

void OatQuickMethodHeader::Dump(const char* prefix) {
    LOGI("%sOatQuickMethodHeader(0x%lx)\n", prefix, Ptr());
    LOGI("%s  code_offset: 0x%lx\n", prefix, GetCodeStart());
    LOGI("%s  code_size: 0x%x\n", prefix, GetCodeSize());
}

void OatQuickMethodHeader::NterpDump() {
    Android::OatPrepare();
    LOGI("  * art::OatQuickMethodHeader::NterpWithClinitImpl: %lx\n", GetNterpWithClinitImpl().Ptr());
    LOGI("  * art::OatQuickMethodHeader::NterpImpl: 0x%lx\n", GetNterpImpl().Ptr());
    LOGI("  * art::OatQuickMethodHeader::NterpMethodHeader: %lx\n", GetNterpMethodHeader().Ptr());
}

} //namespace art
