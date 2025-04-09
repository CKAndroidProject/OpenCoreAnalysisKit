/*
 * Copyright (C) 2025-present, Guanyou.Chen. All rights reserved.
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

#ifndef PARSER_COMMAND_FAKE_EXEC_FAKE_EXECUTABLE_H_
#define PARSER_COMMAND_FAKE_EXEC_FAKE_EXECUTABLE_H_

#include "common/link_map.h"

class FakeExecutable {
public:
    static int OptionExec(int argc, char* const argv[]);
    static bool RebuildExecDynamic(LinkMap* executable);
    static bool RebuildExecDynamic64(LinkMap* executable);
    static bool RebuildExecDynamic32(LinkMap* executable);
    static void Usage();
};

#endif  // PARSER_COMMAND_FAKE_EXEC_FAKE_EXECUTABLE_H_
