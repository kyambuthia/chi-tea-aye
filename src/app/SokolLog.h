#pragma once

#include <cstdint>

namespace app {

void sokolLog(const char* tag, uint32_t log_level, uint32_t log_item_id, const char* message, uint32_t line, const char* filename, void* user_data);

} // namespace app
