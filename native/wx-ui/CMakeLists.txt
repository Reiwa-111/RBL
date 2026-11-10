cmake_minimum_required(VERSION 3.24)
project(RBLStudioWx LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    enable_language(RC)
    find_package(wxWidgets 3.2 CONFIG REQUIRED COMPONENTS base core aui stc)
    set(RBL_WX_LIBRARIES wx::base wx::core wx::aui wx::stc)

    # wx/msw/wx.rc embeds wxWidgets' Common Controls v6 manifest. For MinGW,
    # wxUSE_DPI_AWARE_MANIFEST=2 selects its PerMonitorV2 manifest variant.
    get_target_property(_rbl_wx_include_dirs wx::core INTERFACE_INCLUDE_DIRECTORIES)
    set(_rbl_wx_rc "")
    set(_rbl_wx_include_root "")
    foreach(_include_dir IN LISTS _rbl_wx_include_dirs)
        if(EXISTS "${_include_dir}/wx/msw/wx.rc")
            set(_rbl_wx_rc "${_include_dir}/wx/msw/wx.rc")
            set(_rbl_wx_include_root "${_include_dir}")
            break()
        endif()
    endforeach()
    if(NOT _rbl_wx_rc)
        message(FATAL_ERROR "Could not locate wxWidgets wx/msw/wx.rc in wx::core include directories")
    endif()

    # GCC's MinGW specs inject default-manifest.o into every executable. It
    # carries a second, DPI-unaware RT_MANIFEST resource, so remove only that
    # automatic object from the endfile spec when supplying wx.rc explicitly.
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        execute_process(
            COMMAND "${CMAKE_CXX_COMPILER}" -dumpspecs
            OUTPUT_VARIABLE _rbl_mingw_specs
            RESULT_VARIABLE _rbl_specs_result
        )
        if(NOT _rbl_specs_result EQUAL 0)
            message(FATAL_ERROR "Could not inspect MinGW GCC specs to prevent a duplicate default manifest")
        endif()
        set(_rbl_default_manifest_spec "%{!shared:%:if-exists(default-manifest.o%s)}")
        string(FIND "${_rbl_mingw_specs}" "${_rbl_default_manifest_spec}" _rbl_manifest_spec_pos)
        if(_rbl_manifest_spec_pos EQUAL -1)
            message(FATAL_ERROR "MinGW GCC specs changed; cannot safely suppress the default manifest object")
        endif()
        string(REPLACE "${_rbl_default_manifest_spec}" "" _rbl_mingw_specs "${_rbl_mingw_specs}")
        set(_rbl_mingw_specs_file "${CMAKE_CURRENT_BINARY_DIR}/rbl-wx-no-default-manifest.specs")
        file(WRITE "${_rbl_mingw_specs_file}" "${_rbl_mingw_specs}")
        set(RBL_WX_NO_DEFAULT_MANIFEST_SPECS "${_rbl_mingw_specs_file}")
    endif()
else()
    find_package(wxWidgets 3.2 REQUIRED COMPONENTS base core aui stc)
    include(${wxWidgets_USE_FILE})
    set(RBL_WX_LIBRARIES ${wxWidgets_LIBRARIES})
endif()

add_executable(rbl-studio-wx WIN32
    src/main.cpp
    src/main_frame.cpp
    src/file_metadata.cpp
)
target_include_directories(rbl-studio-wx PRIVATE include)
target_link_libraries(rbl-studio-wx PRIVATE ${RBL_WX_LIBRARIES})
target_compile_definitions(rbl-studio-wx PRIVATE wxUSE_STC=1)

if(WIN32)
    target_sources(rbl-studio-wx PRIVATE "${_rbl_wx_rc}")
    target_include_directories(rbl-studio-wx PRIVATE "${_rbl_wx_include_root}")
    target_compile_definitions(rbl-studio-wx PRIVATE
        "$<$<COMPILE_LANGUAGE:RC>:wxUSE_DPI_AWARE_MANIFEST=2>"
        "$<$<COMPILE_LANGUAGE:RC>:wxUSE_RC_MANIFEST=1>"
    )
    if(RBL_WX_NO_DEFAULT_MANIFEST_SPECS)
        target_link_options(rbl-studio-wx PRIVATE "-specs=${RBL_WX_NO_DEFAULT_MANIFEST_SPECS}")
    endif()
endif()

if(BUILD_TESTING)
    add_executable(rbl-studio-app-root-test tests/app_root_test.cpp)
    target_include_directories(rbl-studio-app-root-test PRIVATE include)
    add_test(NAME rbl-studio-app-root COMMAND rbl-studio-app-root-test)
    add_executable(rbl-studio-theme-palette-test tests/theme_palette_test.cpp)
    target_include_directories(rbl-studio-theme-palette-test PRIVATE include)
    target_link_libraries(rbl-studio-theme-palette-test PRIVATE ${RBL_WX_LIBRARIES})
    add_test(NAME rbl-studio-theme-palette COMMAND rbl-studio-theme-palette-test)
    if(UNIX)
        add_executable(rbl-studio-posix-file-metadata-test
            ../../tests/posix_file_metadata.cpp
            src/file_metadata.cpp
        )
        target_include_directories(rbl-studio-posix-file-metadata-test PRIVATE include)
        target_compile_options(rbl-studio-posix-file-metadata-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
        add_test(NAME rbl-studio-posix-file-metadata COMMAND rbl-studio-posix-file-metadata-test)
    endif()
    if(WIN32)
        find_program(RBL_POWERSHELL_EXECUTABLE powershell)
        if(RBL_POWERSHELL_EXECUTABLE)
            add_test(NAME rbl-studio-windows-registry-policy
                COMMAND "${RBL_POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
                    -File "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/test_windows_registry_policy.ps1")
            add_test(NAME rbl-studio-windows-save-cleanup
                COMMAND "${RBL_POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
                    -File "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/test_windows_save_cleanup.ps1")
        endif()
    endif()
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME rbl-studio-dpi-static
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/dpi_static_test.py")
        add_test(NAME rbl-studio-quick-open-static
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/quick_open_static_test.py")
        add_test(NAME rbl-studio-find-replace-static
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/find_replace_static_test.py")
        add_test(NAME rbl-studio-visual-consistency-static
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/visual_consistency_static_test.py")
        add_test(NAME rbl-studio-save-security-static
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/test_wx_save_dirfd_security.py")
        add_test(NAME rbl-studio-worker-lifecycle-static
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/test_wx_worker_lifecycle.py")
    endif()
endif()

if(MSVC)
    target_compile_options(rbl-studio-wx PRIVATE /W4 /permissive-)
else()
    target_compile_options(rbl-studio-wx PRIVATE -Wall -Wextra -Wpedantic)
endif()

