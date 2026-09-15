# Prevent re-defining the package target
if(TARGET DLSS::DLSS)
  return()
endif()

if(DEFINED DLSS_ROOT)
    find_path(DLSS_INCLUDE_DIR
              nvsdk_ngx.h
              NO_DEFAULT_PATH
              PATHS "${DLSS_ROOT}"
              PATH_SUFFIXES "include"
              DOC "path to NVIDIA DLSS SDK header files"
    )
    if(WIN32)
        # Use the dynamic-CRT (MD/MDd) import library to match Aurora's /MD runtime.
        # nvsdk_ngx_d = dynamic CRT release, nvsdk_ngx_d_dbg = dynamic CRT debug.
        find_library(DLSS_LIBRARY
                     nvsdk_ngx_d
                     NO_DEFAULT_PATH
                     PATHS "${DLSS_ROOT}/lib"
                     DOC "path to NVIDIA DLSS NGX loader library (dynamic CRT)"
        )
        find_library(DLSS_LIBRARY_DEBUG
                     nvsdk_ngx_d_dbg
                     NO_DEFAULT_PATH
                     PATHS "${DLSS_ROOT}/lib"
                     DOC "path to NVIDIA DLSS NGX loader library debug (dynamic CRT)"
        )
    endif()
else()
    find_path(DLSS_INCLUDE_DIR
              nvsdk_ngx.h
              PATH_SUFFIXES "include"
              DOC "path to NVIDIA DLSS SDK header files"
    )
    find_library(DLSS_LIBRARY nvsdk_ngx_d)
    find_library(DLSS_LIBRARY_DEBUG nvsdk_ngx_d_dbg)
endif()

cmake_path(GET DLSS_INCLUDE_DIR PARENT_PATH DLSS_INSTALL_PREFIX)

add_library(DLSS::DLSS STATIC IMPORTED)

if(DLSS_LIBRARY)
    set_property(TARGET DLSS::DLSS APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
    set_target_properties(DLSS::DLSS PROPERTIES IMPORTED_LOCATION_RELEASE "${DLSS_LIBRARY}")
endif()
if(DLSS_LIBRARY_DEBUG)
    set_property(TARGET DLSS::DLSS APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
    set_target_properties(DLSS::DLSS PROPERTIES IMPORTED_LOCATION_DEBUG "${DLSS_LIBRARY_DEBUG}")
endif()

set_target_properties(DLSS::DLSS PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${DLSS_INCLUDE_DIR}"
)

if(WIN32 AND DEFINED DLSS_ROOT)
    # Collect the runtime DLLs to stage next to the executable.
    # Release and development DLLs share names, so stage only one set to avoid overwriting files.
    # Use development DLLs when explicitly requested; use release DLLs otherwise.
    file(GLOB DLSS_RUNTIME_DLLS_REL "${DLSS_ROOT}/bin/rel/*.dll")
    file(GLOB DLSS_RUNTIME_DLLS_DEV "${DLSS_ROOT}/bin/dev/*.dll")
    option(USE_DLSS_DEVELOPMENT_DLLS
        "Stage NVIDIA's development DLSS DLLs, which add validation but watermark the image." OFF)
    if(USE_DLSS_DEVELOPMENT_DLLS AND DLSS_RUNTIME_DLLS_DEV)
        message(STATUS "Using DLSS development DLLs (output will be watermarked).")
        set(DLSS_RUNTIME_DLLS ${DLSS_RUNTIME_DLLS_DEV})
    elseif(DLSS_RUNTIME_DLLS_REL)
        set(DLSS_RUNTIME_DLLS ${DLSS_RUNTIME_DLLS_REL})
    else()
        set(DLSS_RUNTIME_DLLS ${DLSS_RUNTIME_DLLS_DEV})
    endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(DLSS
    DEFAULT_MSG
    DLSS_INCLUDE_DIR
    DLSS_LIBRARY
)

mark_as_advanced(
    DLSS_INCLUDE_DIR
    DLSS_LIBRARY
    DLSS_LIBRARY_DEBUG
    DLSS_RUNTIME_DLLS
)
