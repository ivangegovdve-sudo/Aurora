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

// Stub implementations of the FidelityFX high-level API.
//
// Compiled in place of the real loader when the FidelityFX signed binaries are absent from the
// externals tree. Every entry point fails, so initFSR() throws and FSR reports itself unsupported.

#include "pch.h"

#include <ffx_api.h>

FFX_API_ENTRY ffxReturnCode_t ffxCreateContext(
    ffxContext* context, ffxCreateContextDescHeader* desc, const ffxAllocationCallbacks* memCb)
{
    (void)context;
    (void)desc;
    (void)memCb;
    return FFX_API_RETURN_ERROR; // No FSR provider: SDK not fully built.
}

FFX_API_ENTRY ffxReturnCode_t ffxDestroyContext(
    ffxContext* context, const ffxAllocationCallbacks* memCb)
{
    (void)context;
    (void)memCb;
    return FFX_API_RETURN_OK;
}

FFX_API_ENTRY ffxReturnCode_t ffxConfigure(ffxContext* context, const ffxConfigureDescHeader* desc)
{
    (void)context;
    (void)desc;
    return FFX_API_RETURN_ERROR;
}

FFX_API_ENTRY ffxReturnCode_t ffxQuery(ffxContext* context, ffxQueryDescHeader* desc)
{
    (void)context;
    (void)desc;
    return FFX_API_RETURN_ERROR;
}

FFX_API_ENTRY ffxReturnCode_t ffxDispatch(ffxContext* context, const ffxDispatchDescHeader* desc)
{
    (void)context;
    (void)desc;
    return FFX_API_RETURN_ERROR;
}
