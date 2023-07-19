cmake_minimum_required(VERSION 3.11.0)
project(bede VERSION 0.1.0)

find_package(fmt REQUIRED)
find_package(cxxopts REQUIRED)
file(GLOB_RECURSE SOURCE_FILES src/*.cpp)
execute_process(COMMAND uname -r OUTPUT_VARIABLE arch OUTPUT_STRIP_TRAILING_WHITESPACE)
set(LINUX_SOURCE /lib/modules/${arch}/build/)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_FLAGS "-O3")

find_library(BPF_LIB NAMES bpf PATHS /usr/lib /usr/local/lib REQUIRED)

add_executable(bede ${SOURCE_FILES})
include_directories(bede include)
target_link_libraries(bede bpf elf fmt::fmt cxxopts::cxxopts)

add_executable(sendrss src/sendrss.c)
target_link_libraries(bede bpf elf fmt::fmt cxxopts::cxxopts)

function(bpf prefix)
    add_custom_target(${prefix}_bpf ALL
            COMMAND clang --target=bpf -nostdinc -S -I/usr/include/linux -I${CMAKE_SOURCE_DIR}/include -I${LINUX_SOURCE}/arch/x86/include -I/usr/include -I${LINUX_SOURCE}/arch/x86/include/uapi -I${LINUX_SOURCE}/arch/x86/include/generated -I${LINUX_SOURCE}/arch/x86/include/generated/uapi -I${LINUX_SOURCE}/include -I${LINUX_SOURCE}/include/uapi -I${LINUX_SOURCE}/include/generated/uapi -I${LINUX_SOURCE}/tools/testing/selftests/bpf -include ${LINUX_SOURCE}/include/linux/kconfig.h -D__KERNEL__ -D__ASM_SYSREG_H -D__BPF_TRACING__ -D__TARGET_ARCH_x86 -Wno-implicit-function-declaration -O3 -emit-llvm -g -c ${CMAKE_SOURCE_DIR}/src/${prefix}.c -o ${CMAKE_BINARY_DIR}/${prefix}.ll
            COMMAND llc -march=bpf -filetype=obj -opaque-pointers -o ${CMAKE_BINARY_DIR}/${prefix}.o ${CMAKE_BINARY_DIR}/${prefix}.ll
            )
    add_dependencies(bede ${prefix}_bpf)
endfunction()

bpf(watermark)

add_subdirectory(microbench)
