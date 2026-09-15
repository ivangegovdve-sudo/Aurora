// Copyright 2026 Autodesk, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Delegates to the NRD SDK's own shader header. A no-op when ENABLE_NRD is 0.
//
// NRD.hlsli is resolved two ways:
//   - slangc at build time, via -I <NRD_ROOT>/Shaders/Include (added by minifyShadersFolder.py)
//   - the Transpiler at runtime, via the embedded copy registered in PTShaderLibrary.cpp
#if ENABLE_NRD
#ifndef COMPILER_DXC
#define COMPILER_DXC
#endif
#include "NRD.hlsli"
#endif
