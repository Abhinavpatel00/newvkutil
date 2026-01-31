#include <sys/stat.h>
#include "tinytypes.h"
static bool file_exists(const char* path)
{
    FILE* f = fopen(path, "rb");
    if(!f)
        return false;
    fclose(f);
    return true;
}

static bool read_file(const char* path, void** out_data, size_t* out_size)
{
    *out_data = NULL;
    *out_size = 0;

    FILE* f = fopen(path, "rb");
    if(!f)
    {
        log_error("Failed to open '%s'", path);
        return false;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);

    if(len <= 0)
    {
        log_error("Invalid size for '%s'", path);
        fclose(f);
        return false;
    }

    void* data = malloc((size_t)len);
    if(!data)
    {
        log_error("Out of memory reading '%s'", path);
        fclose(f);
        return false;
    }

    if(fread(data, 1, (size_t)len, f) != (size_t)len)
    {
        log_error("Short read for '%s'", path);
        free(data);
        fclose(f);
        return false;
    }

    fclose(f);
    *out_data = data;
    *out_size = (size_t)len;
    return true;
}



static char* dup_string(const char* s)
{
    if(!s)
        return NULL;

    size_t len = strlen(s);
    char*  out = (char*)malloc(len + 1);
    if(!out)
        return NULL;

    memcpy(out, s, len + 1);
    return out;
}

static bool ends_with(const char* s, const char* suffix)
{
    if(!s || !suffix)
        return false;

    size_t sl = strlen(s);
    size_t su = strlen(suffix);

    if(su > sl)
        return false;

    return strcmp(s + (sl - su), suffix) == 0;
}

// compiledshaders/foo.frag.spv -> shaders/foo.frag
static bool spv_to_source_path(char* out, size_t out_cap, const char* spv_path)
{
    if(!spv_path || !out || out_cap == 0)
        return false;

    if(!ends_with(spv_path, ".spv"))
        return false;

    const char* prefix_spv = "compiledshaders/";
    const char* prefix_src = "shaders/";

    if(strncmp(spv_path, prefix_spv, strlen(prefix_spv)) != 0)
        return false;

    const char* rest = spv_path + strlen(prefix_spv);

    size_t rest_len = strlen(rest);
    if(rest_len <= 4)
        return false;

    size_t new_len = rest_len - 4;  // strip ".spv"

    int n = snprintf(out, out_cap, "%s%.*s", prefix_src, (int)new_len, rest);
    return (n > 0 && (size_t)n < out_cap);
}

static uint64_t file_mtime_ns(const char* path)
{
    if(!path)
        return 0;

    struct stat st;
    if(stat(path, &st) != 0)
        return 0;

#if defined(__APPLE__)
    return (uint64_t)st.st_mtimespec.tv_sec * 1000000000ull + (uint64_t)st.st_mtimespec.tv_nsec;
#else
    return (uint64_t)st.st_mtim.tv_sec * 1000000000ull + (uint64_t)st.st_mtim.tv_nsec;
#endif
}

static bool compile_glsl_to_spv(const char* src_path, const char* spv_path)
{
    if(!src_path || !spv_path)
        return false;

    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "glslc \"%s\" -o \"%s\" 2> compiledshaders/shader_errors.txt", src_path, spv_path);

    int r = system(cmd);
    if(r != 0)
    {
        log_error("glslc failed: %s -> %s", src_path, spv_path);

        FILE* f = fopen("compiledshaders/shader_errors.txt", "rb");
        if(f)
        {
            char   buf[1024];
            size_t n = fread(buf, 1, sizeof(buf) - 1, f);
            buf[n]   = 0;
            log_error("glslc error: %s", buf);
            fclose(f);
        }
        return false;
    }

    return true;
}


