#include "tinytypes.h"
static bool file_exists(const char* path)
{
    FILE* f = fopen(path, "rb");
    if(!f)
        return false;
    fclose(f);
    return true;
}
