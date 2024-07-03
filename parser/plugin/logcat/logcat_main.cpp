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
#include "logger/log.h"
#include "android.h"
#include "command/command_manager.h"
#include "command/command.h"
#include "common/auxv.h"
#include "log.h"
#include "LogBuffer.h"
#include "LogStatistics.h"
#include "SerializedData.h"
#include "SerializedLogBuffer.h"
#include "cxx/list.h"
#include <string>
#include <unistd.h>
#include <getopt.h>

using namespace android;

class LogcatCommand : public Command {
public:
    static constexpr int DUMP_MAIN = 1 << 0;
    static constexpr int DUMP_RADIO = 1 << 1;
    static constexpr int DUMP_EVENTS = 1 << 2;
    static constexpr int DUMP_SYSTEM = 1 << 3;
    static constexpr int DUMP_CRASH = 1 << 4;
    static constexpr int DUMP_KERNEL = 1 << 5;

    LogcatCommand() : Command("logcat") {}
    ~LogcatCommand() {}
    int main(int argc, char* const argv[]);
    bool prepare(int argc, char* const argv[]) { return true; }
    void usage();
private:
    int dump_flag = 0;
    int filter = 0;
    int id;
};

void __attribute__((constructor)) logcat_init(void) {
    CommandManager::PushExtendCommand(new LogcatCommand());
}

void __attribute__((destructor)) logcat_fini(void) {}

// std::list<SerializedLogChunk> logs_[LOG_ID_MAX] GUARDED_BY(logd_lock);
static void PrintSerializedLogBuf(const char* header, cxx::list& logs, int filter, int id) {
    LOGI("%s\n", header);
    for (const auto& value : logs) {
        SerializedData content = value;
        if (!content.data())
            continue;
        content.DecodeDump(filter, id);
    }
}

static SerializedLogBuffer AnalysisSerializedLogBuffer() {
    SerializedLogBuffer serial = 0x0;
    api::MemoryRef exec_text = CoreApi::FindAuxv(AT_ENTRY);
    api::MemoryRef exec_fn = CoreApi::FindAuxv(AT_EXECFN);
    exec_text.Prepare(false);
    exec_fn.Prepare(false);
    uint32_t point_size = CoreApi::GetPointSize();

    auto callback = [&](LoadBlock *block) -> bool {
        if (!(block->flags() & Block::FLAG_W))
            return false;

        SerializedLogBuffer buffer(block->vaddr(), block);
        do {
            api::MemoryRef vtbl = buffer.valueOf();
            if (vtbl.IsValid()) {
                bool match = true;
                // virtual method
                for (int k = 0; k < MEMBER_SIZE(SerializedLogBuffer, vtbl); ++k) {
                    if (!exec_text.Block()->virtualContains(vtbl.valueOf(k * point_size))) {
                        match = false;
                        break;
                    }
                }

                // log_buffer = new SerializedLogBuffer(&reader_list, &log_tags, &log_statistics);
                if (match) {
                    match &= exec_fn.Block()->virtualContains(buffer.reader_list());
                    match &= exec_fn.Block()->virtualContains(buffer.tags());
                    match &= exec_fn.Block()->virtualContains(buffer.stats());
                }

                if (match) {
                    serial = buffer;
                    serial.copyRef(buffer);
                    return true;
                }
            }

            buffer.MovePtr(point_size);
        } while (buffer.Ptr() + SIZEOF(SerializedLogBuffer) < block->vaddr() + block->size());
        return false;
    };
    CoreApi::ForeachLoadBlock(callback);
    return serial;
}

int LogcatCommand::main(int argc, char* const argv[]) {
    if (CoreApi::Bits() != 64) {
        LOGE("ERROR: Not support logcat on bit32 system.\n");
        return 0;
    }

    int opt;
    int option_index = 0;
    optind = 0; // reset
    static struct option long_options[] = {
        {"buffer",    required_argument,  0,  'b'},
        {"pid",       required_argument,  0,  'p'},
        {"uid",       required_argument,  0,  'u'},
        {"tid",       required_argument,  0,  't'},
    };

    while ((opt = getopt_long(argc, argv, "b:p:u:t:",
                long_options, &option_index)) != -1) {
        switch (opt) {
            case 'b':
                if (!strcmp(optarg, "main"))
                    dump_flag |= DUMP_MAIN;

                if (!strcmp(optarg, "radio"))
                    dump_flag |= DUMP_RADIO;

                if (!strcmp(optarg, "events"))
                    dump_flag |= DUMP_EVENTS;

                if (!strcmp(optarg, "system"))
                    dump_flag |= DUMP_SYSTEM;

                if (!strcmp(optarg, "crash"))
                    dump_flag |= DUMP_CRASH;

                if (!strcmp(optarg, "kernel"))
                    dump_flag |= DUMP_KERNEL;

                if (!strcmp(optarg, "all")) {
                    dump_flag = DUMP_MAIN | DUMP_RADIO
                              | DUMP_EVENTS | DUMP_SYSTEM
                              | DUMP_CRASH | DUMP_KERNEL;
                }
                break;
            case 'p':
                filter = SerializedData::FILTER_PID;
                id = std::atoi(optarg);
                break;
            case 'u':
                filter = SerializedData::FILTER_UID;
                id = std::atoi(optarg);
                break;
            case 't':
                filter = SerializedData::FILTER_TID;
                id = std::atoi(optarg);
                break;
        }
    }

    if (!dump_flag) dump_flag = DUMP_MAIN | DUMP_SYSTEM | DUMP_CRASH | DUMP_KERNEL;

    LogStatistics::Init();
    SerializedData::Init();
    uint32_t point_size = CoreApi::GetPointSize();

    if (Android::Sdk() >= Android::S) {
        SerializedLogBuffer::Init31();
        SerializedLogBuffer log_buffer = AnalysisSerializedLogBuffer();

        if (dump_flag & DUMP_MAIN) {
            cxx::list main_logs = log_buffer.logs() + LOG_ID_MAIN * SIZEOF(cxx_list);
            PrintSerializedLogBuf("--------- beginning of main", main_logs, filter, id);
        }

        if (dump_flag & DUMP_RADIO) {
            cxx::list radio_logs = log_buffer.logs() + LOG_ID_RADIO * SIZEOF(cxx_list);
            PrintSerializedLogBuf("--------- beginning of radio", radio_logs, filter, id);
        }

        if (dump_flag & DUMP_EVENTS) {
            cxx::list events_logs = log_buffer.logs() + LOG_ID_EVENTS * SIZEOF(cxx_list);
            PrintSerializedLogBuf("--------- beginning of events", events_logs, filter, id);
        }

        if (dump_flag & DUMP_SYSTEM) {
            cxx::list system_logs = log_buffer.logs() + LOG_ID_SYSTEM * SIZEOF(cxx_list);
            PrintSerializedLogBuf("--------- beginning of system", system_logs, filter, id);
        }

        if (dump_flag & DUMP_CRASH) {
            cxx::list crash_logs = log_buffer.logs() + LOG_ID_CRASH * SIZEOF(cxx_list);
            PrintSerializedLogBuf("--------- beginning of crash", crash_logs, filter, id);
        }

        if (dump_flag & DUMP_KERNEL) {
            cxx::list kernel_logs = log_buffer.logs() + LOG_ID_KERNEL * SIZEOF(cxx_list);
            PrintSerializedLogBuf("--------- beginning of kernel", kernel_logs, filter, id);
        }
    } else {
        LogBuffer::Init();
    }
    return 0;
}

void LogcatCommand::usage() {
    LOGI("Usage: logcat [option]...\n");
    LOGI("Option:\n");
    LOGI("    -b, --buffer=<buffer> {main, radio, events, system, crash, kernel}\n");
    LOGI("    -p, --pid=<pid>\n");
    LOGI("    -u, --uid=<uid>\n");
    LOGI("    -t, --tid=<tid>\n");
}
