#ifndef TINYTYPES_H_
#define TINYTYPES_H_
#include "pcg.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stb/stb_ds.h"
#define VK_NO_PROTOTYPES
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include "external/cgltf/cgltf.h"
#define GLFW_EXPOSE_NATIVE_X11
#define GLFW_EXPOSE_NATIVE_WAYLAND
#include <GLFW/glfw3native.h>
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#include "external/VulkanMemoryAllocator/include/vk_mem_alloc.h"
#include "external/volk/volk.h"
#include "external/cglm/include/cglm/cglm.h"
#include "external/logger-c/logger/logger.h"

#include <stdbool.h>
#define FLOW_API
#define u32 uint32_t
#define MAX_FRAME_IN_FLIGHT 3
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define CLAMP(v, mn, mx) MIN(MAX(v, mn), mx)
uint32_t round_up(uint32_t a, uint32_t b);
uint64_t round_up_64(uint64_t a, uint64_t b);
#define FLOW_ARRAY_COUNT(array)                                                                                        \
    (sizeof(array) / (sizeof(array[0]) * (sizeof(array) != PTR_SIZE || sizeof(array[0]) <= PTR_SIZE)))

#define VK_CHECK(x)                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        VkResult err = (x);                                                                                            \
        if(err != VK_SUCCESS)                                                                                          \
        {                                                                                                              \
            log_fatal("Vulkan error %d at %s:%d", err, __FILE__, __LINE__);                                            \
            abort();                                                                                                   \
        }                                                                                                              \
    } while(0)

#define SWAP(type, a, b) do {     \
    type _temp = (a);             \
    (a) = (b);                    \
    (b) = _temp;                  \
} while (0)

// Usage: SWAP(int, x, y);
#define ARRAY_COUNT(array) (sizeof(array)) / (sizeof(array[1]))

#define forEach(i, count) for(uint32_t i = 0; i < (count); i++)
#if defined(_MSC_VER) && !defined(__clang__)
#if !defined(_DEBUG) && !defined(NDEBUG)
#define NDEBUG
#endif

#define UNREF_PARAM(x) ((void)(x))
#define ALIGNAS(x) __declspec(align(x))
#define DEFINE_ALIGNED(def, a) __declspec(align(a)) def
#define FORGE_CALLCONV __cdecl
#define ALIGNOF(x) __alignof(x)
#define THREAD_LOCAL __declspec(thread)


#include <BaseTsd.h>
typedef SSIZE_T ssize_t;

#if defined(_M_X64)
#define ARCH_X64
#define ARCH_X86_FAMILY
#elif defined(_M_IX86)
#define ARCH_X86
#define ARCH_X86_FAMILY
#else
#error "Unsupported architecture for msvc compiler"
#endif

// Msvc removes trailing commas
#define OPT_COMMA_VA_ARGS(...) , __VA_ARGS__

#elif defined(__GNUC__) || defined(__clang__)
#include <assert.h>
#include <stdalign.h>
#include <sys/types.h>

#ifdef __OPTIMIZE__
// Some platforms define NDEBUG for Release builds
#if !defined(NDEBUG) && !defined(_DEBUG)
#define NDEBUG
#endif
#else
#if !defined(_DEBUG) && !defined(NDEBUG)
#define _DEBUG
#endif
#endif

#ifdef __APPLE__
#define NOREFS __unsafe_unretained
#endif

#define UNREF_PARAM(x) ((void)(x))
#define ALIGNAS(x) __attribute__((aligned(x)))
#define DEFINE_ALIGNED(def, a) __attribute__((aligned(a))) def
#define FORGE_CALLCONV
#define ALIGNOF(x) __alignof__(x)
#define THREAD_LOCAL __thread

#if defined(__cplusplus)
    #define COMPILE_ASSERT(exp) static_assert((exp), #exp)
#else
    #define COMPILE_ASSERT(exp) _Static_assert((exp), #exp)
#endif

#endif

#endif  // TINYTYPES_H_
