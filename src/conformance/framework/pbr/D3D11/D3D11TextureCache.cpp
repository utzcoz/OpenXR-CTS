// Copyright 2023-2024, The Khronos Group Inc.
//
// Based in part on code that is:
//
// Copyright (C) Microsoft Corporation.  All Rights Reserved
// Licensed under the MIT License. See License.txt in the project root for license information.
//
// SPDX-License-Identifier: MIT AND Apache-2.0

#if defined(XR_USE_GRAPHICS_API_D3D11)

#include "D3D11TextureCache.h"

#include "D3D11Texture.h"

#include "../PbrMaterial.h"
#include "../PbrTexture.h"

#include <array>
#include <memory>
#include <mutex>
#include <stdexcept>

namespace Pbr
{
    D3D11TextureCache::D3D11TextureCache() : m_cacheMutex(std::make_unique<std::mutex>())
    {
    }

    ComPtr<ID3D11ShaderResourceView> D3D11TextureCache::CreateTypedSolidColorTexture(const Pbr::D3D11Resources& pbrResources,
                                                                                     XrColor4f color, bool sRGB)
    {
        const std::array<uint8_t, 4> rgba = LoadRGBAUI4(color);

        // Check cache to see if this flat texture already exists.
        const uint32_t colorKey = *reinterpret_cast<const uint32_t*>(rgba.data());
        {
            std::lock_guard<std::mutex> guard(*m_cacheMutex);
            auto textureIt = m_solidColorTextureCache.find(colorKey);
            if (textureIt != m_solidColorTextureCache.end()) {
                return textureIt->second;
            }
        }

        auto formatParams = Conformance::Image::FormatParams::R8G8B8A8(sRGB);
        auto metadata = Conformance::Image::ImageLevelMetadata::MakeUncompressed(1, 1);
        auto image = Conformance::Image::Image{formatParams, {{metadata, rgba}}};

        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> texture = Pbr::D3D11Texture::CreateTexture(pbrResources, image);

        std::lock_guard<std::mutex> guard(*m_cacheMutex);
        // If the key already exists then the existing texture will be returned.
        return m_solidColorTextureCache.emplace(colorKey, texture).first->second;
    }
}  // namespace Pbr

#endif  // defined(XR_USE_GRAPHICS_API_D3D11)
