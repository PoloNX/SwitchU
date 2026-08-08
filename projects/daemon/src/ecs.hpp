#pragma once
#include <switch.h>
#include <cstdint>

namespace switchu::daemon {

void initializeExternalContentAllocator();
Result registerExternalContent(uint64_t program_id, const char* exefs_path);
Result unregisterExternalContent(uint64_t program_id);

}
