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

#include "runtime/runtime.h"
#include "android.h"

namespace art {

Runtime& Runtime::Current() {
    Runtime& runtime = Android::GetRuntime();
    if (!runtime.Ptr()) {
        api::MemoryRef value = Android::SearchSymbol(Android::ART_RUNTIME_INSTANCE);
        if (CoreApi::GetPointSize() == 64) {
            runtime = *reinterpret_cast<uint64_t *>(value.Real());
        } else {
            runtime = *reinterpret_cast<uint32_t *>(value.Real());
        }
    }
    return runtime;
}

} // namespace art
