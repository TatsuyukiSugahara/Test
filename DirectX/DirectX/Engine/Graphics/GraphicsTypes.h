#pragma once
#include <cstdint>
#include "../Math/Vector.h"
#include "../Math/Matrix.h"


namespace engine
{
	namespace graphics
	{
		/**
		 * API 非依存のプリミティブトポロジー列挙
		 * D3D11: D3D11_PRIMITIVE_TOPOLOGY_*
		 * D3D12: D3D12_PRIMITIVE_TOPOLOGY_TYPE_*
		 * Vulkan: VkPrimitiveTopology
		 */
		enum class PrimitiveTopology : uint8_t
		{
			TriangleList,
			TriangleStrip,
			LineList,
			LineStrip,
			PointList,
		};


		/**
		 * API 非依存の Map 種別
		 * D3D11: D3D11_MAP_*
		 */
		enum class MapType : uint8_t
		{
			Read,
			Write,
			ReadWrite,
			WriteDiscard,
			WriteNoOverwrite,
		};


		/**
		 * API 非依存の Map 結果
		 * D3D11: D3D11_MAPPED_SUBRESOURCE
		 */
		struct MappedSubresource
		{
			void*    pData      = nullptr;
			uint32_t rowPitch   = 0;
			uint32_t depthPitch = 0;
		};


		/**
		 * テクスチャフィルタリングモード
		 * D3D11: D3D11_FILTER_*
		 */
		enum class FilterMode : uint8_t
		{
			MinMagMipPoint,
			MinMagMipLinear,
			MinMagLinearMipPoint,
			Anisotropic,
		};


		/**
		 * テクスチャアドレッシングモード
		 * D3D11: D3D11_TEXTURE_ADDRESS_*
		 */
		enum class AddressMode : uint8_t
		{
			Clamp,
			Wrap,
			Mirror,
			Border,
		};


		/**
		 * API 非依存のサンプラー設定
		 * D3D11: D3D11_SAMPLER_DESC
		 */
		struct SamplerDesc
		{
			FilterMode  filter     = FilterMode::MinMagMipLinear;
			AddressMode addressU   = AddressMode::Clamp;
			AddressMode addressV   = AddressMode::Clamp;
			AddressMode addressW   = AddressMode::Clamp;
			float       mipLODBias = 0.0f;
			uint32_t    maxAniso   = 1;
			float       minLOD     = 0.0f;
			float       maxLOD     = 3.402823466e+38f;
		};


		/**
		 * ピクセルフォーマット (API 非依存)
		 * D3D11: DXGI_FORMAT_* への変換は D3D11 層の ToD3D11Format() が担う
		 */
		enum class PixelFormat : uint8_t
		{
			Unknown,
			// 8bit 4ch
			R8G8B8A8_Unorm,
			R8G8B8A8_Unorm_SRGB,
			B8G8R8A8_Unorm,
			B8G8R8A8_Unorm_SRGB,
			// Depth/Stencil
			D24_Unorm_S8_Uint,
			// Float
			R16G16B16A16_Float,
			R32_Float,
			R32G32B32A32_Float,
			// BC 圧縮
			BC1_Unorm,
			BC1_Unorm_SRGB,
			BC2_Unorm,
			BC2_Unorm_SRGB,
			BC3_Unorm,
			BC3_Unorm_SRGB,
			BC4_Unorm,
			BC5_Unorm,
			BC6H_UFloat16,
			BC7_Unorm,
			BC7_Unorm_SRGB,
		};


		/**
		 * マルチサンプル設定
		 * D3D11: DXGI_SAMPLE_DESC
		 */
		struct SampleDesc
		{
			uint32_t count   = 1;
			uint32_t quality = 0;
		};


		/** 頂点データ構造体 (API 非依存) */
		struct VertexData
		{
			math::Vector3 position;
			math::Vector3 normal;
			math::Vector2 uv;
		};

		/** 頂点シェーダー用定数バッファ構造体 */
		struct VSConstantBuffer
		{
			math::Matrix4x4 world;
			math::Matrix4x4 view;
			math::Matrix4x4 projection;
		};


		/** テクスチャ生成記述子 (API 非依存) */
		struct Texture2DDesc
		{
			uint32_t    width     = 0;
			uint32_t    height    = 0;
			uint32_t    arraySize = 1;
			uint32_t    mipLevels = 1;
			bool        isCubemap = false;
			PixelFormat format    = PixelFormat::Unknown;
		};

		/** CPU 側の画像サブリソースデータ (API 非依存) */
		struct ImageSubresourceData
		{
			const void* pixels     = nullptr;
			uint32_t    rowPitch   = 0;
			uint32_t    slicePitch = 0;
		};

		/** CPU 側の画像データ (API 非依存) */
		struct ImageData
		{
			const void* pixels     = nullptr;
			uint32_t    rowPitch   = 0;
			uint32_t    slicePitch = 0;
			const ImageSubresourceData* subresources = nullptr;
			uint32_t subresourceCount = 0;
		};


		/**
		 * プラットフォーム非依存のウィンドウハンドル
		 * Win32:  handle に HWND を void* キャストして格納
		 * macOS:  NSWindow*
		 * Wayland: wl_surface*
		 */
		struct NativeWindowHandle
		{
			void* handle = nullptr;
		};
	}
}
