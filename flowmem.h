#ifndef FLOWMEM_H_
#define FLOWMEM_H_

#include "external/rpmalloc/rpmalloc/rpmalloc.h"

#define MAX(a, b) (((a) > (b)) ? (a) : (b))

void* flow_malloc_internal(size_t size, const char* f, int l, const char* sf);
void* flow_memalign_internal(size_t align, size_t size, const char* f, int l, const char* sf);
void* flow_calloc_internal(size_t count, size_t size, const char* f, int l, const char* sf);
void* flow_calloc_memalign_internal(size_t count, size_t align, size_t size, const char* f, int l, const char* sf);
void* flow_realloc_internal(void* ptr, size_t size, const char* f, int l, const char* sf);
void flow_free_internal(void* ptr, const char* f, int l, const char* sf);
// PLATFORM
#define PTR_SIZE 8
#define PLATFORM_MIN_MALLOC_ALIGNMENT (PTR_SIZE * 2)
#define VECTORMATH_MIN_ALIGN 16
#define MIN_ALLOC_ALIGNMENT MAX(VECTORMATH_MIN_ALIGN, PLATFORM_MIN_MALLOC_ALIGNMENT)

void flow_mem_thread_init(void)
{
   rpmalloc_thread_initialize();
}

void flow_mem_thread_shutdown(void)
{
   rpmalloc_thread_finalize();
}
void flow_memory_init(void)
{
   rpmalloc_initialize(NULL);
}

void flow_memory_shutdown(void)
{
   rpmalloc_finalize();
}
void* flow_malloc_internal(size_t size, const char* f, int l, const char* sf)
{
   (void)f;
   (void)l;
   (void)sf;
   return flow_memalign_internal(MIN_ALLOC_ALIGNMENT, size, f, l, sf);
}

void* flow_calloc_internal(size_t count, size_t size, const char* file, int line, const char* source_func)
{
   (void)file;
   (void)line;
   (void)source_func;
   return flow_calloc_memalign_internal(count, MIN_ALLOC_ALIGNMENT, size, file, line, source_func);
}

void* flow_memalign_internal(size_t align, size_t size, const char* file, int line, const char* source_func)
{
   (void)file;
   (void)line;
   (void)source_func;
   return rpaligned_alloc(align, size);
}

void* flow_calloc_memalign_internal(size_t count, size_t align, size_t size, const char* file, int line, const char* source_func)
{
   (void)file;
   (void)line;
   (void)source_func;
   return rpaligned_calloc(align, count, size);
}

void* flow_realloc_internal(void* ptr, size_t size, const char* file, int line, const char* source_func)
{
   (void)file;
   (void)line;
   (void)source_func;
   return rprealloc(ptr, size);
}

void flow_free_internal(void* ptr, const char* file, int line, const char* source_func)
{
   (void)file;
   (void)line;
   (void)source_func;
   rpfree(ptr);
}

#ifndef flow_malloc
#define flow_malloc(size) flow_malloc_internal(size, __FILE__, __LINE__, __FUNCTION__)
#endif
#ifndef flow_memalign
#define flow_memalign(align, size) flow_memalign_internal(align, size, __FILE__, __LINE__, __FUNCTION__)
#endif
#ifndef flow_calloc
#define flow_calloc(count, size) flow_calloc_internal(count, size, __FILE__, __LINE__, __FUNCTION__)
#endif
#ifndef flow_calloc_memalign
#define flow_calloc_memalign(count, align, size) flow_calloc_memalign_internal(count, align, size, __FILE__, __LINE__, __FUNCTION__)
#endif
#ifndef flow_realloc
#define flow_realloc(ptr, size) flow_realloc_internal(ptr, size, __FILE__, __LINE__, __FUNCTION__)
#endif
#ifndef flow_free
#define flow_free(ptr) flow_free_internal(ptr, __FILE__, __LINE__, __FUNCTION__)
#endif

#ifdef __cplusplus
#ifndef flow_new
#define flow_new(ObjectType, ...) flow_new_internal<ObjectType>(__FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__)
#endif
#ifndef flow_delete
#define flow_delete(ptr) flow_delete_internal(ptr, __FILE__, __LINE__, __FUNCTION__)
#endif
#endif

#endif // FLOWMEM_H_
