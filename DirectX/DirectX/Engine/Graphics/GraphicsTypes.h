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
		 * ピクセルフォーマット
		 * D3D11: DXGI_FORMAT_*
		 */
		enum class PixelFormat : uint8_t
		{
			Unknown,
			R8G8B8A8_Unorm,
			D24_Unorm_S8_Uint,
			R32_Float,
			R32G32B32A32_Float,
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
	}
}
