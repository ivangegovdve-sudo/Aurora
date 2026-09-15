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
#pragma once

/// Returns usable asset path: resolved path if available, otherwise authored path.
inline std::string GetUsableAssetPath(const pxr::SdfAssetPath& assetPath)
{
    const std::string& resolvedPath = assetPath.GetResolvedPath();
    return resolvedPath.empty() ? assetPath.GetAssetPath() : resolvedPath;
}

/// Returns the usable file path held by a VtValue, or empty string if not an SdfAssetPath.
inline std::string GetUsableAssetPath(const pxr::VtValue& value)
{
    if (!value.IsHolding<pxr::SdfAssetPath>())
        return std::string();

    return GetUsableAssetPath(value.UncheckedGet<pxr::SdfAssetPath>());
}
