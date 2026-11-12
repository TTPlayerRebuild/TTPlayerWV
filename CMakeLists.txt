cmake_minimum_required(VERSION 3.24)
project(TtpWv LANGUAGES C CXX RC)
set(TTP_PLUGIN_ID ttp_wv)
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "ttp_wv requires MSVC and -A Win32")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(cmake/legacy_windows.cmake)
include(cmake/size_release.cmake)
include(cmake/codec.cmake)
include(cmake/version.cmake)
add_library(ttp_wv SHARED src/plugin.cpp src/reader.cpp src/decoder.cpp src/ttp_wv.def
  "${CMAKE_CURRENT_BINARY_DIR}/generated/version.rc")
add_dependencies(ttp_wv ttp_wv_version)
target_include_directories(ttp_wv PUBLIC include PRIVATE src)
target_compile_definitions(ttp_wv PRIVATE TTP_NAMESPACE=wv TTP_WAVPACK=1 UNICODE _UNICODE
  NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0501 _CRT_SECURE_NO_WARNINGS)
target_compile_options(ttp_wv PRIVATE /W4 /permissive- /EHsc /GR- /utf-8 /fp:precise /arch:SSE2)
target_link_libraries(ttp_wv PRIVATE WavPack::WavPack ole32 uuid shlwapi)
target_link_options(ttp_wv PRIVATE /ENTRY:DllMainCRTStartupForYY_Thunks
  /alternatename:_YY_ThunksOriginalDllMainCRTStartup@12=__DllMainCRTStartup@12)
set_target_properties(ttp_wv PROPERTIES PREFIX "" OUTPUT_NAME "ttp_wv")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_command(TARGET ttp_wv POST_BUILD
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_legacy_imports.py"
    "$<TARGET_FILE:ttp_wv>"
    --exports "${ttplayer_yy_thunks_SOURCE_DIR}/Config/x86/5.1.2600.txt"
    --exports "${ttplayer_yy_thunks_SOURCE_DIR}/Config/x86/6.1.7600.txt"
    --report "$<TARGET_FILE_DIR:ttp_wv>/legacy-imports.json" VERBATIM)
install(TARGETS ttp_wv RUNTIME DESTINATION AddIn COMPONENT Runtime)
