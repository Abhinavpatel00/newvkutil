#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// stage: matches VkShaderStageFlagBits
bool vk_compile_slang(const char* source_file, 
                      const char* entry_point, 
                      int stage, 
                      void** out_spv, 
                      size_t* out_size);

#ifdef __cplusplus
}
#endif
