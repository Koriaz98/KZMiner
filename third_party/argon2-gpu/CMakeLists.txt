# argon2-gpu (vendorise, sous-ensemble CUDA uniquement)
# Source : https://github.com/WebDollar/argon2-gpu
# Modifications KZMiner par rapport a l'upstream :
#  - architectures CUDA ciblees : Turing/Ampere/Ada/Blackwell (via CUDA_ARCHITECTURES)
#  - le bug d'ordre d'initialisation "invalid resource handle" (runner construit
#    avant cudaSetDevice()) est contourne cote appelant : cudaSetDevice(index)
#    est toujours appele AVANT de construire tout objet CUDA (GlobalContext,
#    ProgramContext, ProcessingUnit) dans notre propre code, jamais dans la lib.

enable_language(CUDA)
find_package(CUDAToolkit REQUIRED)

add_library(argon2-gpu-common STATIC
    lib/argon2-gpu-common/argon2params.cpp
    lib/argon2-gpu-common/blake2b.cpp
)
target_include_directories(argon2-gpu-common PUBLIC
    include
)
target_include_directories(argon2-gpu-common PRIVATE
    include/argon2-gpu-common
    lib/argon2-gpu-common
)

add_library(argon2-cuda-vendored STATIC
    lib/argon2-cuda/device.cpp
    lib/argon2-cuda/globalcontext.cpp
    lib/argon2-cuda/programcontext.cpp
    lib/argon2-cuda/processingunit.cpp
    lib/argon2-cuda/kernels.cu
)

target_include_directories(argon2-cuda-vendored PUBLIC
    include
)
target_include_directories(argon2-cuda-vendored PRIVATE
    include/argon2-cuda
    lib/argon2-cuda
)

target_compile_definitions(argon2-cuda-vendored PUBLIC HAVE_CUDA=1)

set_target_properties(argon2-cuda-vendored PROPERTIES
    CUDA_SEPARABLE_COMPILATION ON
    POSITION_INDEPENDENT_CODE ON
    CUDA_ARCHITECTURES "75;86;89;120"
)

target_link_libraries(argon2-cuda-vendored PUBLIC
    argon2-gpu-common
    CUDA::cudart
)
