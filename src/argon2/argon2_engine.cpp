#include "argon2_engine.h"
#include <argon2.h>
#include <stdexcept>
#include <cstring>
#include <sys/mman.h>

namespace
{
    thread_local uint8_t* g_memory = nullptr;
    thread_local size_t   g_memory_size = 0;

    uint8_t* allocate_hugepage(size_t bytes)
    {
        void* ptr = mmap(
            nullptr,
            bytes,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS,
            -1,
            0
        );

        if(ptr == MAP_FAILED)
        {
            throw std::runtime_error("mmap failed for Argon2 buffer");
        }

        madvise(ptr, bytes, MADV_HUGEPAGE);
        return static_cast<uint8_t*>(ptr);
    }

    int persistent_allocate(uint8_t** memory, size_t bytes)
    {
        if(g_memory == nullptr || g_memory_size != bytes)
        {
            if(g_memory != nullptr)
            {
                munmap(g_memory, g_memory_size);
            }
            g_memory = allocate_hugepage(bytes);
            g_memory_size = bytes;
        }
        *memory = g_memory;
        return ARGON2_OK;
    }

    void persistent_free(uint8_t* memory, size_t bytes)
    {
        (void)memory;
        (void)bytes;
    }
}

std::vector<uint8_t> Argon2Engine::hash(
    const std::vector<uint8_t>& header,
    uint32_t t_cost,
    uint32_t m_cost_kib
)
{
    std::vector<uint8_t> output(32);

    // Salt officiel BTC09 (core/pow.go: powSalt = "BTC09/pow/v1")
    static const uint8_t salt[] = {
        'B','T','C','0','9','/','p','o','w','/','v','1'
    };

    argon2_context context;
    memset(&context, 0, sizeof(context));

    context.out       = output.data();
    context.outlen    = static_cast<uint32_t>(output.size());
    context.pwd       = const_cast<uint8_t*>(header.data());
    context.pwdlen    = static_cast<uint32_t>(header.size());
    context.salt      = const_cast<uint8_t*>(salt);
    context.saltlen   = sizeof(salt);
    context.secret    = nullptr;
    context.secretlen = 0;
    context.ad        = nullptr;
    context.adlen     = 0;
    context.t_cost    = t_cost;
    context.m_cost    = m_cost_kib;
    context.lanes     = 1;
    context.threads   = 1;
    context.version   = ARGON2_VERSION_13;
    context.allocate_cbk = persistent_allocate;
    context.free_cbk     = persistent_free;
    context.flags         = ARGON2_DEFAULT_FLAGS;

    int result = argon2id_ctx(&context);

    if(result != ARGON2_OK)
    {
        throw std::runtime_error(argon2_error_message(result));
    }

    return output;
}
