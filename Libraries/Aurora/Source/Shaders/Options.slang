#ifndef __OPTIONS_H__
#define __OPTIONS_H__

// HGI backend sets this to 0; guard default so external -D wins.
#ifndef ENABLE_RUNTIME_COMPILE_EVALUATE_MATERIAL_FUNCTION
#if defined(ENABLE_MDL) && ENABLE_MDL
#define ENABLE_RUNTIME_COMPILE_EVALUATE_MATERIAL_FUNCTION 0
#else
#define ENABLE_RUNTIME_COMPILE_EVALUATE_MATERIAL_FUNCTION 1
#endif
#endif

// Whether the NRD packing paths are compiled in DirectX backend.
#ifndef ENABLE_NRD
#define ENABLE_NRD 0
#endif

#define ENABLE_VALIDATE_TEXTURE_INDICES 0
#define ENABLE_LAYERS 1
#define DISTANCE_UNIT 3
#define USE_REFERENCE_BSDF 0
#endif // __OPTIONS_H__
