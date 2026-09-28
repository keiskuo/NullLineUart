# ch570q.toolchain.cmake

# 1. 告诉 CMake 目标是裸机，不是 Linux/Windows
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv32)

# 工具链前缀
set(TOOLCHAIN_PREFIX "riscv-wch-elf-")


set(CMAKE_C_COMPILER                ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_ASM_COMPILER              ${CMAKE_C_COMPILER})
set(CMAKE_CXX_COMPILER              ${TOOLCHAIN_PREFIX}g++)
set(CMAKE_LINKER                    ${TOOLCHAIN_PREFIX}g++)
set(CMAKE_OBJCOPY                   ${TOOLCHAIN_PREFIX}objcopy)
set(CMAKE_SIZE                      ${TOOLCHAIN_PREFIX}size)
set(CMAKE_READELF                   ${TOOLCHAIN_PREFIX}readelf)
set(CMAKE_AR                        ${TOOLCHAIN_PREFIX}ar)
set(CMAKE_OBJDUMP                   ${TOOLCHAIN_PREFIX}objdump)

# 裸机必须：编译器测试只编译不链接
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# ---- 公共架构选项（C/C++/ASM 通用）----
# 注意：这里必须使用“空格分隔的字符串”，不能写成 CMake 列表（即多个参数）。
# 如果把每个 flag 写成列表元素，插值到 CMAKE_<LANG>_FLAGS_INIT 后会变成分号
# 分隔（-march=...;-mabi=...），Windows 下 Ninja 会把整串当作一个参数传给 gcc，
# 于是 -march= 会把后面的 flag 全部吃掉，-nostartfiles/--specs=*/-ffunction-sections
# 等都不生效，最终导致链接时 crt0.o 与 startup_CH572.S 的 _start 重复定义。
set(ARCH_FLAGS
    "-march=rv32imc_zba_zbb_zbc_zbs_xw -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore"
)

# ---- 公共编译选项（C/C++ 通用）----
set(COMMON_FLAGS
    "${ARCH_FLAGS} -fmax-errors=20 -fmessage-length=0 -ffunction-sections -fdata-sections -fno-common --param=highcode-gen-section-name=1"
)

# ---- C 编译选项 ----
set(CMAKE_C_FLAGS_INIT "${COMMON_FLAGS} -fsigned-char")

# ---- C++ 编译选项 ----
set(CMAKE_CXX_FLAGS_INIT "${COMMON_FLAGS} -fsigned-char -fabi-version=0")

# ---- 汇编选项 ----
set(CMAKE_ASM_FLAGS_INIT "${COMMON_FLAGS} -x assembler-with-cpp")

# ---- 链接选项（与项目路径无关的部分）----
set(CMAKE_EXE_LINKER_FLAGS_INIT
    "${ARCH_FLAGS} -nostartfiles -Wl,--gc-sections -Wl,--print-memory-usage --specs=nano.specs --specs=nosys.specs"
)
