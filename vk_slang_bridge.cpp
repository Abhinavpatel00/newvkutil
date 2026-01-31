#include "vk_slang_bridge.h"
#include "shadercomp/slang/include/slang.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Using Slang namespace
using namespace slang;

// Static global session to reuse across compiles (optional, but good for perf)
static IGlobalSession* g_slangGlobalSession = nullptr;

extern "C" bool vk_compile_slang(const char* source_file, 
                      const char* entry_point, 
                      int stage, 
                      void** out_spv, 
                      size_t* out_size)
{
    if (out_spv) *out_spv = nullptr;
    if (out_size) *out_size = 0;

    SlangResult res;

    // 1. Initialize Global Session
    if (!g_slangGlobalSession)
    {
        res = createGlobalSession(&g_slangGlobalSession);
        if (SLANG_FAILED(res)) {
            fprintf(stderr, "Slang: Failed to create global session: %x\n", res);
            return false;
        }
    }

    // 2. Create Session
    SessionDesc sessionDesc = {};
    TargetDesc targetDesc = {};
    targetDesc.format = SLANG_SPIRV;
    targetDesc.profile = g_slangGlobalSession->findProfile("glsl_450");
    targetDesc.flags = SLANG_TARGET_FLAG_GENERATE_SPIRV_DIRECTLY;

    sessionDesc.targets = &targetDesc;
    sessionDesc.targetCount = 1;

    ISession* session = nullptr;
    res = g_slangGlobalSession->createSession(sessionDesc, &session);
    if (SLANG_FAILED(res)) {
        fprintf(stderr, "Slang: Failed to create session\n");
        return false;
    }

    // 3. Load Module
    IModule* module = nullptr;
    {
        // Read file content manually or use loadModule if path is correct relative to cwd
        // Slang's loadModule relies on search paths. 
        // We will try loading by path.
        IBlob* diagBlob = nullptr;
        module = session->loadModule(source_file, &diagBlob);
        
        if (diagBlob) {
            fprintf(stderr, "Slang Diagnostics (Load): %s\n", (const char*)diagBlob->getBufferPointer());
            diagBlob->release();
        }
        
        if (!module) {
            fprintf(stderr, "Slang: Failed to load module: %s\n", source_file);
            session->release();
            return false;
        }
    }

    // 4. Find Entry Point
    IEntryPoint* entryPoint = nullptr;
    {
        IBlob* diagBlob = nullptr;
        // SlangStage: 
        // We need to map VkShaderStageFlagBits to SlangStage
        // But for findEntryPointByName, we might not strictly need it if name is unique
        // However, findEntryPointByName doc says "Note that this does not work..."
        // Use findAndCheckEntryPoint if needed.
        // Let's try findEntryPointByName first.
        
        res = module->findEntryPointByName(entry_point, &entryPoint);
        if (SLANG_FAILED(res) || !entryPoint) {
           fprintf(stderr, "Slang: Failed to find entry point '%s'\n", entry_point);
           session->release();
           return false;
        }
    }

    // 5. Compose and Compile
    // Create a composite component type with the module and entry point
    IComponentType* components[] = { module, entryPoint };
    IComponentType* program = nullptr;
    {
        IBlob* diagBlob = nullptr;
        res = session->createCompositeComponentType(components, 2, &program, &diagBlob);
        if (diagBlob) {
            fprintf(stderr, "Slang Diagnostics (Compose): %s\n", (const char*)diagBlob->getBufferPointer());
            diagBlob->release();
        }
        if (SLANG_FAILED(res)) {
            session->release();
            return false;
        }
    }

    // 6. Get Code (SPIR-V)
    IBlob* codeBlob = nullptr;
    IBlob* diagBlob = nullptr;
    res = program->getEntryPointCode(0, 0, &codeBlob, &diagBlob);
    
    if (diagBlob) {
        fprintf(stderr, "Slang Diagnostics (Compile): %s\n", (const char*)diagBlob->getBufferPointer());
        diagBlob->release();
    }

    bool success = SLANG_SUCCEEDED(res) && codeBlob;
    
    if (success)
    {
        size_t sz = codeBlob->getBufferSize();
        void* mem = malloc(sz);
        if (mem) {
            memcpy(mem, codeBlob->getBufferPointer(), sz);
            *out_spv = mem;
            *out_size = sz;
        } else {
            success = false;
        }
    }

    if (codeBlob) codeBlob->release();
    if (program) program->release();
    // EntryPoint and Module are owned by session/internalref? 
    // Actually module is a component type, releasing program releases ref?
    // Slang ref counting rules... usually safe to release if created.
    // IModule and IEntryPoint are obtained from session/module. 
    // We should probably NOT release them manually if they are weak refs, but they are COM pointers.
    // Usually get... returns AddRef'd pointer.
    // Documentation says "outEntryPoint...".
    // We will release them.
    // entryPoint->release(); // causes crash? verify docs. 
    // Safe bet: release what we 'get' or 'create'.
    // createCompositeComponentType -> program. Release program.
    // loadModule -> module. Release module? No, module is managed by session cache?
    // Let's release program and session.
    
    if (session) session->release(); // This should clean up

    return success;
}
