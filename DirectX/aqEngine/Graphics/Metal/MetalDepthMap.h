#pragma once
// Metal の深度専用テクスチャ(シャドウマップ)。
//
// 本ヘッダは Objective-C 型を使うため **Graphics/Metal 配下の .mm からのみ** include できる
// (MetalCommon.h が __OBJC__ を要求する)。エンジンから見える口は IDepthMap だけ。
#if defined(ENGINE_GRAPHICS_METAL)
#include "Graphics/Metal/MetalCommon.h"
#include "Graphics/IDepthMap.h"
#include "Graphics/IShaderResourceView.h"
#include "Graphics/ISamplerState.h"


namespace aq
{
	namespace graphics
	{
		/**
		 * Metal 深度専用テクスチャ(設計書/MetalBackend設計.md §7)
		 *
		 * D3D12DepthMap / VulkanDepthMap と同等で、Depth32Float の 2D 配列(4 スライス)。
		 * - スライス別ビューをシャドウパスの depthAttachment として使う(P4)。
		 * - 全スライスの配列ビューを PSt4 へ、比較サンプラ(LessEqual)を PSs1 へバインドする。
		 *
		 * Apple Silicon は D24_Unorm_S8_Uint を持たないため、フォーマットは
		 * metal::DEPTH_PIXEL_FORMAT(= Depth32Float)で固定する(設計書 §0.2)。
		 */
		class MetalDepthMap final : public IDepthMap
		{
		public:
			/** スライス数(カスケードシャドウの分割数)。D3D12 / Vulkan 版と揃える */
			static constexpr uint32_t ARRAY_SIZE = 4;


			/**
			 * 内蔵ビュー / サンプラ
			 */
		public:
			/** 深度テクスチャ(全スライス配列 or 単一スライス)を指す SRV。所有はしない */
			class DepthSRV final : public IShaderResourceView
			{
			public:
				id<MTLTexture> texture = nil;

				inline id<MTLTexture> GetTexture() const { return texture; }

				/** Metal では id<MTLTexture> をネイティブハンドルとして返す */
				void* GetNativeHandle() const override { return texture; }

				void Release() override {}  // MetalDepthMap が所有
			};


			/**
			 * シャドウ用の比較サンプラ。
			 * MetalSampler(別 TU)へ依存させず MetalDepthMap 内で完結させる
			 * (VulkanDepthMap が VulkanSampler を内蔵しているのと同じ位置づけ)。
			 */
			class CompareSampler final : public ISamplerState
			{
			private:
				id<MTLDevice>       device_;
				id<MTLSamplerState> sampler_;

			public:
				CompareSampler();
				~CompareSampler() override;

				CompareSampler(const CompareSampler&) = delete;
				CompareSampler& operator=(const CompareSampler&) = delete;

				/** Create() の前に MTLDevice を渡しておく */
				inline void SetDevice(id<MTLDevice> device) { device_ = device; }

				bool Create(const SamplerDesc& desc) override;
				void Release() override;

				inline id<MTLSamplerState> GetSamplerState() const { return sampler_; }
			};


		private:
			/** 深度テクスチャ本体(Depth32Float / 2D 配列 4 スライス / Private) */
			id<MTLTexture> texture_;

			/** スライス別のテクスチャビュー(depthAttachment / デバッグ表示用) */
			id<MTLTexture> sliceTextures_[ARRAY_SIZE];

			/** 内蔵ビューとサンプラ */
			DepthSRV       arraySRV_;
			DepthSRV       sliceSRVs_[ARRAY_SIZE];
			CompareSampler sampler_;

			/** 解像度(正方形) */
			uint32_t resolution_;


		public:
			MetalDepthMap();
			~MetalDepthMap() override;

			MetalDepthMap(const MetalDepthMap&) = delete;
			MetalDepthMap& operator=(const MetalDepthMap&) = delete;


			/**
			 * 生成 / 破棄
			 */
		public:
			/**
			 * 深度テクスチャとスライスビュー、比較サンプラを作る
			 * @param device     MTLDevice
			 * @param resolution 1 辺の解像度(正方形)
			 * @return 生成できたら true
			 */
			bool Create(id<MTLDevice> device, const uint32_t resolution);

			void Release();


			/**
			 * IDepthMap
			 */
		public:
			IShaderResourceView* GetSRV() const override { return const_cast<DepthSRV*>(&arraySRV_); }

			IShaderResourceView* GetSliceSRV(uint32_t slice) const override
			{
				return (slice < ARRAY_SIZE) ? const_cast<DepthSRV*>(&sliceSRVs_[slice])
				                            : const_cast<DepthSRV*>(&arraySRV_);
			}

			ISamplerState* GetSampler()    const override { return const_cast<CompareSampler*>(&sampler_); }
			uint32_t       GetResolution() const override { return resolution_; }


			/**
			 * Metal 固有(P4 のシャドウパスが使う)
			 */
		public:
			/** 全スライスを含む深度テクスチャ */
			inline id<MTLTexture> GetTexture() const { return texture_; }

			/** 指定スライスのテクスチャビュー(renderPass の depthAttachment に載せる) */
			inline id<MTLTexture> GetSliceTexture(const uint32_t slice) const
			{
				return (slice < ARRAY_SIZE) ? sliceTextures_[slice] : nil;
			}

			/** 深度のフォーマット。PSO の depthAttachmentPixelFormat に要る */
			inline MTLPixelFormat GetPixelFormat() const { return metal::DEPTH_PIXEL_FORMAT; }

			inline id<MTLSamplerState> GetSamplerState() const { return sampler_.GetSamplerState(); }
		};
	}
}
#endif // ENGINE_GRAPHICS_METAL
