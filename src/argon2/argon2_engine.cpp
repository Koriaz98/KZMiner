#include "argon2_engine.h"
#include <argon2.h>
#include <stdexcept>
#include <cstring>
#include <sys/mman.h>

// Symboles de la variante AVX2 de libargon2, renommee avec le prefixe
// "avx2_" pour coexister dans le meme binaire que la variante portable
// (dont les symboles, sans prefixe, viennent de argon2.h/libargon2_ref.a).
// Meme layout de struct argon2_context des deux cotes (meme definition
// source, juste compilee deux fois avec des jeux d'instructions differents).
extern "C"
{
    int avx2_argon2id_ctx(argon2_context* context);
    const char* avx2_argon2_error_message(int error_code);
}

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

    // Detection CPU au demarrage, mise en cache (une seule fois par
    // processus). __builtin_cpu_supports est une primitive standard du
    // compilateur (GCC/Clang), sans dependance externe.
    bool cpuSupportsAvx2()
    {
        static bool checked = false;
        static bool supported = false;

        if(!checked)
        {
            __builtin_cpu_init();
            supported = __builtin_cpu_supports("avx2")
                     && __builtin_cpu_supports("fma");
            checked = true;
        }

        return supported;
    }
}

std::vector<uint8_t> Argon2Engine::hash(
    const std::vector<uint8_t>& password,
    const std::vector<uint8_t>& salt,
    uint32_t t_cost,
    uint32_t m_cost_kib
)
{
    std::vector<uint8_t> output(32);

    argon2_context context;
    memset(&context, 0, sizeof(context));

    context.out       = output.data();
    context.outlen    = static_cast<uint32_t>(output.size());
    context.pwd       = const_cast<uint8_t*>(password.data());
    context.pwdlen    = static_cast<uint32_t>(password.size());
    context.salt      = const_cast<uint8_t*>(salt.data());
    context.saltlen   = static_cast<uint32_t>(salt.size());
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

    int result = cpuSupportsAvx2()
        ? avx2_argon2id_ctx(&context)
        : argon2id_ctx(&context);

    if(result != ARGON2_OK)
    {
        const char* message = cpuSupportsAvx2()
            ? avx2_argon2_error_message(result)
            : argon2_error_message(result);
        throw std::runtime_error(message);
    }

    return output;
}
