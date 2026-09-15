# Prevent re-defining the package target
if(TARGET FSR::FSR)
  return()
endif()

# FidelityFX SDK v2.x ships two integration paths:
#   (A) Signed binary DLLs in signedbin/ -- preferred; no shader compilation needed.
#   (B) Source-level integration via api/ + upscalers/ + backend/dx12/ -- requires DXC to
#       generate *_permutations.h shader blob headers; not used here.
#
# Install layout:
#   FSR_ROOT/api/include/             (ffx_api.h, dx12/ffx_api_dx12.h, ...)
#   FSR_ROOT/upscalers/include/       (ffx_upscale.h)
#   FSR_ROOT/upscalers/fsr3/include/  (ffx_fsr3upscaler.h and gpu/ subdir)
#   FSR_ROOT/signedbin/               (amd_fidelityfx_loader_dx12.dll/.lib, upscaler DLL, ...)

if(DEFINED FSR_ROOT)
    find_path(FSR_FSR3_INCLUDE_DIR
              ffx_fsr3upscaler.h
              NO_DEFAULT_PATH
              PATHS "${FSR_ROOT}"
              PATH_SUFFIXES "upscalers/fsr3/include"
              DOC "path to FidelityFX FSR3 upscaler headers"
    )
    find_path(FSR_API_INCLUDE_DIR
              ffx_api.h
              NO_DEFAULT_PATH
              PATHS "${FSR_ROOT}"
              PATH_SUFFIXES "api/include"
              DOC "path to FidelityFX API headers"
    )
    find_path(FSR_UPSCALER_INCLUDE_DIR
              ffx_upscale.h
              NO_DEFAULT_PATH
              PATHS "${FSR_ROOT}"
              PATH_SUFFIXES "upscalers/include"
              DOC "path to FidelityFX upscaler common headers"
    )
    # Signed-binary loader import library (v2.x preferred integration path)
    find_library(FSR_LOADER_LIB
                 NAMES "amd_fidelityfx_loader_dx12"
                 NO_DEFAULT_PATH
                 PATHS "${FSR_ROOT}/signedbin"
                 DOC "FidelityFX loader import library"
    )
    # Collect all DLLs from signedbin/ for runtime deployment
    file(GLOB FSR_DLLS "${FSR_ROOT}/signedbin/*.dll")
else()
    find_path(FSR_FSR3_INCLUDE_DIR
              ffx_fsr3upscaler.h
              PATH_SUFFIXES "upscalers/fsr3/include"
              DOC "path to FidelityFX FSR3 upscaler headers"
    )
    find_path(FSR_API_INCLUDE_DIR
              ffx_api.h
              PATH_SUFFIXES "api/include"
              DOC "path to FidelityFX API headers"
    )
    find_path(FSR_UPSCALER_INCLUDE_DIR
              ffx_upscale.h
              PATH_SUFFIXES "upscalers/include"
              DOC "path to FidelityFX upscaler common headers"
    )
    find_library(FSR_LOADER_LIB
                 NAMES "amd_fidelityfx_loader_dx12"
                 PATH_SUFFIXES "signedbin"
                 DOC "FidelityFX loader import library"
    )
endif()

set(FSR_INCLUDE_DIRS
    "${FSR_FSR3_INCLUDE_DIR}"
    "${FSR_API_INCLUDE_DIR}"
    "${FSR_UPSCALER_INCLUDE_DIR}"
)

# Expose loader lib status to callers
if(FSR_LOADER_LIB)
    set(FSR_HAS_SIGNED_BINARY TRUE CACHE BOOL "FidelityFX signed binary loader is available" FORCE)
else()
    set(FSR_HAS_SIGNED_BINARY FALSE CACHE BOOL "FidelityFX signed binary loader is available" FORCE)
endif()

add_library(FSR::FSR INTERFACE IMPORTED)
set_target_properties(FSR::FSR PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${FSR_INCLUDE_DIRS}"
)
if(FSR_LOADER_LIB)
    set_target_properties(FSR::FSR PROPERTIES
        INTERFACE_LINK_LIBRARIES "${FSR_LOADER_LIB}"
    )
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FSR
    DEFAULT_MSG
    FSR_FSR3_INCLUDE_DIR
    FSR_API_INCLUDE_DIR
)

mark_as_advanced(
    FSR_FSR3_INCLUDE_DIR
    FSR_API_INCLUDE_DIR
    FSR_UPSCALER_INCLUDE_DIR
    FSR_LOADER_LIB
    FSR_HAS_SIGNED_BINARY
    FSR_INCLUDE_DIRS
    FSR_DLLS
)
