cmake_minimum_required(VERSION 3.12)
project(portablebuildtools LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(SOURCE source/pbt.cpp source/base.cpp source/miniz/miniz.c)
set(RESOURCE source/pbt.rc)
set_source_files_properties(source/miniz/miniz.c PROPERTIES
 COMPILE_DEFINITIONS "MINIZ_NO_DEFLATE_APIS;MINIZ_NO_ZLIB_APIS;MINIZ_NO_STDIO;MINIZ_NO_TIME;MINIZ_NO_MALLOC;MINIZ_NO_ZLIB_COMPATIBLE_NAMES")
add_executable(${PROJECT_NAME} WIN32 ${SOURCE} ${RESOURCE})
set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE FALSE LINKER_LANGUAGE CXX)
target_link_libraries(${PROJECT_NAME} PRIVATE
Ole32 Comctl32 Wininet Shlwapi Advapi32)
if(MSVC)
 target_compile_definitions(${PROJECT_NAME} PRIVATE _CRT_SECURE_NO_WARNINGS)
 target_compile_options(${PROJECT_NAME} PRIVATE
  /utf-8 /W3 /O1 /Gy /Gw /GS-)
 target_link_libraries(${PROJECT_NAME} PRIVATE ucrt msvcrt vcruntime)
 target_link_options(${PROJECT_NAME} PRIVATE
  /MANIFEST:NO /ENTRY:_start /INCREMENTAL:NO /OPT:REF /OPT:ICF)
else()
 target_compile_options(${PROJECT_NAME} PRIVATE
  $<$<NOT:$<CONFIG:Debug>>:-Oz>
  -ffunction-sections -fdata-sections -flto -fmerge-all-constants
  -fno-asynchronous-unwind-tables -fno-unwind-tables)
 target_compile_options(${PROJECT_NAME} PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
  $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
  $<$<COMPILE_LANGUAGE:CXX>:-fno-threadsafe-statics>
  $<$<COMPILE_LANGUAGE:CXX>:-fno-use-cxa-atexit>
  $<$<COMPILE_LANGUAGE:CXX>:-fno-c++-static-destructors>)
 target_link_options(${PROJECT_NAME} PRIVATE
  $<$<NOT:$<CONFIG:DEBUG>>:-s>)
 target_link_options(${PROJECT_NAME} PRIVATE
  -nostdlib++ -flto -Wl,--gc-sections -Wl,--icf=all)
 target_link_options(${PROJECT_NAME} PRIVATE
  -static -nostartfiles -Wl,--entry=_start)
endif()
