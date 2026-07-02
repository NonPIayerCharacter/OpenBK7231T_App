cmake_minimum_required(VERSION 3.15)
set(TARGET_EXE flasher_stub)
set(TARGET_PROJECT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/project)

add_definitions(
    -DEXEC_USING_STD_PRINTF
    -DTDEFL_LESS_MEMORY
#    -DUSE_EXTERNAL_MZCRC
    -DMINIZ_LITTLE_ENDIAN=1
    -DMINIZ_USE_UNALIGNED_LOADS_AND_STORES=1
#    -DMINIZ_NO_INFLATE_APIS
    -DMINIZ_NO_TIME
    -DMINIZ_NO_STDIO
)

add_compile_options(
    -Os
    -g0
    -std=gnu11
)

add_executable(${TARGET_EXE})

target_include_directories(${TARGET_EXE}
    PRIVATE
        ${PROJECT_SOURCE_DIR}/ROM-EXPORT/mbedtls-2.17.0-rom/include
	../../../platforms/RTL8721DA/flasher_stub
)

set_target_properties(${TARGET_EXE}
    PROPERTIES
        SUFFIX ".elf"
        ARCHIVE_OUTPUT_DIRECTORY "${TARGET_PROJECT_DIR}/cmake/lib"
        LIBRARY_OUTPUT_DIRECTORY "${TARGET_PROJECT_DIR}/cmake/lib"
        RUNTIME_OUTPUT_DIRECTORY "${TARGET_PROJECT_DIR}/cmake/bin"
)

target_sources(${TARGET_EXE}
    PRIVATE
        stub.c
        flash.c
#	tlsf.c
	../../../libraries/miniz/miniz.c
	../../../libraries/miniz/miniz_tdef.c
	../../../libraries/miniz/miniz_tinfl.c
    )

target_sources(${TARGET_EXE}
    PRIVATE
        ${PROJECT_SOURCE_DIR}/MSDK/plf/GD32VW55x_standard_peripheral/Source/gd32vw55x_eclic.c
        ${PROJECT_SOURCE_DIR}/MSDK/plf/GD32VW55x_standard_peripheral/Source/gd32vw55x_efuse.c
        ${PROJECT_SOURCE_DIR}/MSDK/plf/GD32VW55x_standard_peripheral/Source/gd32vw55x_fmc.c
        ${PROJECT_SOURCE_DIR}/MSDK/plf/GD32VW55x_standard_peripheral/Source/gd32vw55x_gpio.c
        ${PROJECT_SOURCE_DIR}/MSDK/plf/GD32VW55x_standard_peripheral/Source/gd32vw55x_hau.c
        ${PROJECT_SOURCE_DIR}/MSDK/plf/GD32VW55x_standard_peripheral/Source/gd32vw55x_hau_sha_md5.c
        ${PROJECT_SOURCE_DIR}/MSDK/plf/GD32VW55x_standard_peripheral/Source/gd32vw55x_rcu.c
        ${PROJECT_SOURCE_DIR}/MSDK/plf/GD32VW55x_standard_peripheral/Source/gd32vw55x_usart.c
    )

target_sources(${TARGET_EXE}
    PRIVATE
        ${PROJECT_SOURCE_DIR}/MSDK/plf/riscv/env/handlers.c
        ${PROJECT_SOURCE_DIR}/MSDK/plf/riscv/env/env_init.c
        ${PROJECT_SOURCE_DIR}/MSDK/plf/riscv/env/entry.S
        ${PROJECT_SOURCE_DIR}/MSDK/plf/src/init_rom_symbol.c
        ${PROJECT_SOURCE_DIR}/MSDK/plf/riscv/env/start.S
        ${PROJECT_SOURCE_DIR}/MSDK/plf/riscv/arch/lib/lib_hook_mbl.c
        ${PROJECT_SOURCE_DIR}/MSDK/plf/riscv/gd32vw55x/system_gd32vw55x.c
    )

target_add_scatter_file(${TARGET_EXE}
    ${TARGET_PROJECT_DIR}/stub.ld
)

target_link_directories(${TARGET_EXE}
    PUBLIC ${PROJECT_SOURCE_DIR}/MSDK/lib
)

target_link_options(${TARGET_EXE}
    PUBLIC
    -Wl,--just-symbols=${PROJECT_SOURCE_DIR}/ROM-EXPORT/symbol/rom_symbol_m.gcc
    -Wl,-Map=${TARGET_PROJECT_DIR}/cmake/bin/${TARGET_EXE}.map
    -Wl,--wrap=malloc
#    -Wl,--wrap=realloc
#    -Wl,--wrap=free
)
#target_link_libraries(${TARGET_EXE}
#    PRIVATE
#        -Wl,--start-group
#        rf
#    -Wl,--end-group
#    )
add_custom_command(TARGET ${TARGET_EXE} POST_BUILD
    COMMAND  cd ${TARGET_PROJECT_DIR}/cmake/bin && ${CMAKE_OBJDUMP} --source --all-headers --demangle --line-numbers --wide ${TARGET_EXE}.elf > ${TARGET_EXE}.lst
    COMMAND  cd ${TARGET_PROJECT_DIR}/cmake/bin && ${CMAKE_OBJCOPY} -O ihex  ${TARGET_EXE}.elf  ${TARGET_EXE}.hex
    COMMAND  cd ${TARGET_PROJECT_DIR}/cmake/bin && ${CMAKE_OBJCOPY} -O binary  ${TARGET_EXE}.elf  ${TARGET_EXE}.bin
    COMMAND  cd ${TARGET_PROJECT_DIR}/cmake/bin && ${CMAKE_OBJDUMP} -d  ${TARGET_EXE}.elf > ${TARGET_EXE}.asm
    COMMAND  cd ${TARGET_PROJECT_DIR}/cmake/bin && gzip ${TARGET_EXE}.bin && mv ${TARGET_EXE}.bin.gz GD32VW553_Stub.bin
    COMMENT "Building ${TARGET_EXE}"
)
