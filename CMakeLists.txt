cmake_minimum_required(VERSION 3.12)
project(portablebuildtools)
set(SOURCE source/pbt.c source/miniz/miniz.c)
set(RESOURCE source/pbt.rc)
add_executable(${PROJECT_NAME} WIN32 ${SOURCE} ${RESOURCE})
set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE FALSE)
target_link_libraries(${PROJECT_NAME} PRIVATE
Ole32 Comctl32 Wininet Shlwapi uuid)
target_link_options(${PROJECT_NAME} PRIVATE
$<$<NOT:$<CONFIG:DEBUG>>:-s>)
target_link_options(${PROJECT_NAME} PRIVATE
-static -nostartfiles -Wl,--entry=_start)
