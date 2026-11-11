#pragma once
#include <memory>
#include <string>
#include "Math/Vector.h"
#include "Graphics/ISamplerState.h"
#include "Graphics/IShaderResourceView.h"
#include "Resource/Resource.h"


namespace aq
{
	namespace rendering
	{
		/**
		 * IBL 用 定数バッファ (b2)。
		 * perDrawCBPool (= sizeof(VSConstantBuffer) = 192B) から Allocate するため 192B に固定する。
		 * UpdateSubresource → UpdateConstantBuffer はバッファのサイズ分を読むので、小さい構造体を渡すと範囲外を読む。
		 * SkyCBData (SkyCommand.h) と同じ作法。PBRLighting.fx の cbuffer IBLCB と先頭を一致させること。
		 */
		struct IBLCBData
		{
			float         intensity      = 1.0f;  // 環境光 (拡散 + 鏡面) 全体の倍率
			float         specularMaxMip = 5.0f;  // 鏡面キューブのミップ数 - 1 (ロード後の TextureData から取る)
			float         _pad0          = 0.0f;
			float         _pad1          = 0.0f;
			math::Vector4 pad[11];                // 192B へのパディング
		};
		static_assert(sizeof(IBLCBData) == 192,
		              "IBLCBData must match perDrawCBPool slot size (192B)");




		/**
		 * IBL (Image Based Lighting) の環境 (設計書/IBL設計.md §1)。
		 *
		 * オフライン生成した 3 本 (irradiance キューブ / 前置フィルタ済み鏡面キューブ / BRDF LUT) の
		 * 非同期ロード要求・サンプラ・強度を持つ。DeferredRenderer が shared_ptr で持ち、
		 * DeferredLightingCommand も shared_ptr で保持する (実行待ちのリストが残る間は資源が生きる。§1.1)。
		 *
		 * 3 本とサンプラは Create() 後に差し替えない。実行時に変わるのは強度だけ。
		 * 生成・ロードに失敗しても落とさない (IsReady() が false のまま、従来のライティングで続行する)。
		 *
		 * スレッド: Create / IsReady / 強度の読み書きはメインスレッド。
		 * レンダースレッドは SRV / サンプラ / GetSpecularMaxMip のゲッタだけを使う
		 * (いずれも IsReady() が true になった後は不変)。
		 */
		class IBLEnvironment
		{
		private:
			/** 描画資源 */
			res::RefGPUResource                      irradiance_;
			res::RefGPUResource                      specular_;
			res::RefGPUResource                      brdfLut_;
			std::unique_ptr<graphics::ISamplerState> sampler_;

			/** ロード要求したパス (ログに出すため保持する) */
			std::string irradiancePath_;
			std::string specularPath_;
			std::string brdfLutPath_;

			/** 環境光 (拡散 + 鏡面) 全体の倍率 */
			float intensity_ = 1.0f;

			/** ロード結果のログを 1 回だけ出すためのフラグ (const 経路から更新する) */
			mutable bool logged_ = false;


		public:
			/**
			 * 3 本のロード要求とサンプラ生成を行う (完了は待たない。IsReady() でポーリングする)。
			 * @param irradiancePath irradiance キューブ (.dds)
			 * @param specularPath   前置フィルタ済み鏡面キューブ (.dds。ミップ全段)
			 * @param brdfLutPath    BRDF LUT (.dds。2D)
			 * @return ロード要求とサンプラ生成ができたら true
			 */
			bool Create(const char* irradiancePath, const char* specularPath, const char* brdfLutPath);

			/** 今フレーム IBL を使えるか (3 本のロード完了と SRV、2 本のキューブが isCubemap であること) */
			bool IsReady() const;


		public:
			inline graphics::IShaderResourceView* GetIrradianceSRV() const { return irradiance_ ? irradiance_->GetShaderResourceView() : nullptr; }
			inline graphics::IShaderResourceView* GetSpecularSRV() const { return specular_ ? specular_->GetShaderResourceView() : nullptr; }
			inline graphics::IShaderResourceView* GetBrdfLutSRV() const { return brdfLut_ ? brdfLut_->GetShaderResourceView() : nullptr; }
			inline graphics::ISamplerState* GetSampler() const { return sampler_.get(); }

			/** 鏡面キューブのミップ数 - 1 (IsReady() が true のときだけ意味を持つ) */
			float GetSpecularMaxMip() const;

			/** 環境光 (拡散 + 鏡面) 全体の倍率。既定 1.0 */
			inline void SetIntensity(const float intensity) { intensity_ = intensity; }
			inline float GetIntensity() const { return intensity_; }


		private:
			/** 1 本のテクスチャが使える状態か (ロード完了・SRV あり・キューブ指定の一致) */
			static bool IsTextureReady(const res::RefGPUResource& texture, const bool expectCubemap);

			/** 3 本のロード結果を 1 回だけログへ出す (全て完了か失敗になってから) */
			void LogOnce() const;

			/** 1 本分のロード結果を 1 行ログへ出す */
			static void LogTexture(const char* label, const std::string& path, const res::RefGPUResource& texture);
		};
	}
}
