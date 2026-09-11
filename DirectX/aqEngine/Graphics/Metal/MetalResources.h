#pragma once
// Metal のテクスチャ / サンプラ / UAV(設計書/MetalBackend設計.md §6)。
//
// **本ヘッダは Objective-C++ 専用**で、Graphics/Metal/ 配下の .mm からのみ include する。
#if defined(ENGINE_GRAPHICS_METAL)
#include "Graphics/Metal/MetalCommon.h"
#include "Graphics/IShaderResourceView.h"
#include "Graphics/ISamplerState.h"
#include "Graphics/IUnorderedAccessView.h"


namespace aq
{
	namespace graphics
	{
		/**
		 * テクスチャ 2D + SRV
		 *
		 * ユニファイドメモリなので MTLStorageModeShared のテクスチャへ
		 * replaceRegion: で直接書き込む(ステージング不要。設計書 §6)。
		 * BC 圧縮はそのまま扱える(Apple Silicon が対応。設計書 §0.2)。
		 */
		class MetalTexture : public IShaderResourceView
		{
		private:
			/** Metal オブジェクト */
			id<MTLDevice>  device_;
			id<MTLTexture> texture_;


		public:
			explicit MetalTexture(id<MTLDevice> device);
			~MetalTexture() override;


		public:
			bool Create(const Texture2DDesc& desc, const ImageData& data);
			void Release() override;

			/** 実体の MTLTexture */
			inline id<MTLTexture> GetTexture() const { return texture_; }

			/**
			 * ImGui の ImTextureID 用ハンドル
			 *
			 * 未ロード時は nullptr を返し、呼び出し側がスキップできるようにする
			 * (D3D12 / Vulkan と同方式)。
			 */
			void* GetNativeHandle() const override;
		};




		/**
		 * サンプラーステート
		 */
		class MetalSampler : public ISamplerState
		{
		private:
			/** Metal オブジェクト */
			id<MTLDevice>       device_;
			id<MTLSamplerState> sampler_;


		public:
			explicit MetalSampler(id<MTLDevice> device);
			~MetalSampler() override;


		public:
			bool Create(const SamplerDesc& desc) override;
			void Release() override;

			/** 実体の MTLSamplerState */
			inline id<MTLSamplerState> GetSampler() const { return sampler_; }
		};




		/**
		 * アンオーダードアクセスビュー
		 *
		 * Metal ではテクスチャ UAV が texture 空間、バッファ UAV が buffer 空間へ落ちる
		 * (設計書 §5.3)ので、両方を持てる入れ物にしておく。
		 * P0〜P4 では使わず、compute を入れる P5 で中身を詰める。
		 */
		class MetalUAV : public IUnorderedAccessView
		{
		private:
			/** Metal オブジェクト(どちらか一方だけが有効) */
			id<MTLTexture> texture_;
			id<MTLBuffer>  buffer_;


		public:
			MetalUAV();
			~MetalUAV() override;


		public:
			void Release() override;

			/** 対象の差し替え(retain / release は本クラスが持つ) */
			void SetTexture(id<MTLTexture> texture);
			void SetBuffer(id<MTLBuffer> buffer);

			inline id<MTLTexture> GetTexture() const { return texture_; }
			inline id<MTLBuffer>  GetBuffer() const  { return buffer_; }
		};
	}
}
#endif // ENGINE_GRAPHICS_METAL
