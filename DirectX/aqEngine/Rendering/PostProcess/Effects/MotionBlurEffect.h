#pragma once
#include <memory>
#include "Rendering/PostProcess/Effects/IPostProcessEffect.h"
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
		 * カメラモーションブラーのエフェクト(旧 MotionBlurPass。設計書/レンダーパイプライン設計.md P2 で改名)。
		 * GBuffer2 (worldPos) と前フレームの viewProj からスクリーン空間速度を再構成し、
		 * シーン RT を速度方向にぼかす。シェーダー / RT が用意できない環境では無効のまま動く。
		 */
		class MotionBlurEffect final : public IPostProcessEffect
		{
		private:
			std::unique_ptr<graphics::IShader>         shader_;
			std::unique_ptr<graphics::IConstantBuffer> constantBuffer_;

			/** ブラー出力 (次段の入力になる) */
			RenderTargetHandle outputRTHandle_;

			/** リサイズ用の一時領域(PrepareResize で作り、CommitResize で本体へ入れ替える) */
			RenderTargetHandle pendingOutputRTHandle_;

			/** 前フレーム viewProj の保持 */
			math::Matrix4x4 prevViewProj_;   // このフレームの CB に使う「前フレーム viewProj」
			math::Matrix4x4 lastViewProj_;   // 次フレームの prev になる値
			bool            prevValid_ = false;

			float strength_ = 0.0f;


		private:
			/** 出力 RT の生成(Initialize / PrepareResize 共用) */
			static RenderTargetHandle CreateOutputRT(const uint32_t width, const uint32_t height);


		public:
			MotionBlurEffect() = default;
			~MotionBlurEffect() override = default;


		public:
			bool Initialize(const uint32_t width, const uint32_t height) override;
			bool IsEnabled(const PostProcessContext& context) const override;

			/**
			 * リサイズ(設計書/エンジン課題解消設計.md P4)。ゲームスレッドで WaitIdle 済みの区間にだけ呼ぶ。
			 * PrepareResize は新寸法の出力 RT を一時領域へ作る(失敗したら片付けて false)。
			 * CommitResize は旧 RT を解放して入れ替える。AbortResize は一時領域を解放する(何度呼んでも安全)。
			 */
			bool PrepareResize(const uint32_t width, const uint32_t height);
			void CommitResize();
			void AbortResize();

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
			/** ブラー強度スケール (0 でエフェクト無効) */
			inline void SetStrength(const float strength) { strength_ = strength < 0.0f ? 0.0f : strength; }
			inline float GetStrength() const { return strength_; }
		};
	}
}
