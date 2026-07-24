cmake_minimum_required(VERSION 3.20)
project(FirmwareTransferCpp VERSION 3.0.0 LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "FirmwareTransferCpp targets Windows WinHTTP, BCrypt, and Winsock APIs.")
endif()

add_executable(FirmwareTransferCpp upload.cpp puff.c)
target_compile_features(FirmwareTransferCpp PRIVATE cxx_std_17)
target_compile_definitions(
    FirmwareTransferCpp
    PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _WIN32_WINNT=0x0601
)
target_link_libraries(FirmwareTransferCpp PRIVATE bcrypt winhttp ws2_32)

if(MSVC)
    set_property(TARGET FirmwareTransferCpp PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(FirmwareTransferCpp PRIVATE /W4 /permissive- /EHsc /utf-8)
    set_source_files_properties(puff.c PROPERTIES COMPILE_OPTIONS /W0)
    target_link_options(FirmwareTransferCpp PRIVATE /INCREMENTAL:NO)
else()
    target_compile_options(FirmwareTransferCpp PRIVATE -Wall -Wextra -Wpedantic)
    set_source_files_properties(puff.c PROPERTIES COMPILE_OPTIONS -w)
    target_link_options(FirmwareTransferCpp PRIVATE -municode -static -static-libgcc -static-libstdc++)
endif()

include(CTest)
if(BUILD_TESTING)
    add_test(NAME transfer_self_test COMMAND FirmwareTransferCpp --self-test --no-color)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(
            NAME transfer_loopback_integration
            COMMAND
                "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration_test.py"
                "$<TARGET_FILE:FirmwareTransferCpp>"
        )
        set_tests_properties(transfer_loopback_integration PROPERTIES TIMEOUT 30)
    endif()
endif()

install(TARGETS FirmwareTransferCpp RUNTIME DESTINATION .)
