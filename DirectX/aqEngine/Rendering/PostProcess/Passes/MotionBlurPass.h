#pragma once
#include <memory>
#include "Rendering/PostProcess/IPostProcessPass.h"
#include "Rendering/RenderTargetHandle.h"
#include "Graphics/IShader.h"
#include "Graphics/IBuffer.h"
#include "Math/Matrix.h"


namespace aq
{
	namespace rendering
	{
		struct CameraData;


		/**
		 * カメラモーションブラーのパス。
		 * GBuffer2 (worldPos) と前フレームの viewProj からスクリーン空間速度を再構成し、
		 * シーン RT を速度方向にぼかす。シェーダー / RT が用意できない環境では無効のまま動く。
		 */
		class MotionBlurPass final : public IPostProcessPass
		{
		private:
			std::unique_ptr<graphics::IShader>         shader_;
			std::unique_ptr<graphics::IConstantBuffer> constantBuffer_;

			/** ブラー出力 (次段の入力になる) */
			RenderTargetHandle outputRTHandle_;

			/** 前フレーム viewProj の保持 */
			math::Matrix4x4 prevViewProj_;   // このフレームの CB に使う「前フレーム viewProj」
			math::Matrix4x4 lastViewProj_;   // 次フレームの prev になる値
			bool            prevValid_ = false;

			float strength_ = 0.0f;


		public:
			MotionBlurPass() = default;
			~MotionBlurPass() override = default;


		public:
			bool Initialize(const uint32_t width, const uint32_t height) override;
			bool IsEnabled(const PostProcessContext& context) const override;

			RenderTargetHandle Build(
				RenderCommandList&        outList,
				const PostProcessContext& context,
				const RenderTargetHandle  input) override;

			/**
			 * メインパスのカメラを受け取り、CB に使う前フレーム viewProj を確定させる
			 * @param camera このフレームのカメラ
			 */
			void SetFrameCamera(const CameraData& camera);


			/**
			 * パラメータ
			 */
		public:
			/** ブラー強度スケール (0 でパス無効) */
			inline void SetStrength(const float strength) { strength_ = strength < 0.0f ? 0.0f : strength; }
			inline float GetStrength() const { return strength_; }
		};
	}
}
