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
		 * 被写界深度のエフェクト(設計書/被写界深度設計.md)。
		 * worldPos (GBuffer2) からビュー空間の z を作り、画素ごとの符号付き CoC で入力をぼかす。
		 * ぼかしは 1 パスの集め方式。デバッグ表示ではぼかしの代わりに CoC を色で出す。
		 * 画素ごとの符号付きぼけ半径(画素)を CoC RT にも書く(OutlinePass が読む。設計書 §0.5)。
		 * オートフォーカスを有効にすると、DoFFocus.fx が画面中央の z から 1×1 のフォーカス RT を更新し、
		 * DoF.fx がそこからピント位置を読む(CPU へは読み戻さない。設計書 §0.2)。
		 * フォーカス側の資源が用意できないときはオートフォーカスだけ使えず、手動のピントで DoF は続ける。
		 * シェーダーか出力 RT / CoC RT が用意できない環境では Initialize は成功を返し、無効のまま入力を素通しする
		 * (MotionBlurEffect と同じ作法)。
		 */
		class DoFEffect final : public IPostProcessEffect
		{
		private:
			std::unique_ptr<graphics::IShader>         shader_;
			std::unique_ptr<graphics::IConstantBuffer> constantBuffer_;

			/** DoF の出力(次段の入力になる) */
			RenderTargetHandle outputRTHandle_;
			/** 符号付きぼけ半径(画素、R32_Float、全解像度)。DoFPass が DoFCoC として掲示板に載せる */
			RenderTargetHandle cocRTHandle_;

			/** オートフォーカス(DoFFocus.fx)。どれかが欠けたらオートフォーカスは使えない */
			std::unique_ptr<graphics::IShader>         focusShader_;
			std::unique_ptr<graphics::IConstantBuffer> focusConstantBuffer_;
			/** フォーカス RT(ピント位置 = ビュー空間の z、R32_Float、1×1)。寸法に依らないのでリサイズで作り直さない */
			RenderTargetHandle focusRTHandle_;

			/** リサイズ用の一時領域(PrepareResize で作り、CommitResize で本体へ入れ替える) */
			RenderTargetHandle pendingOutputRTHandle_;
			RenderTargetHandle pendingCoCRTHandle_;

			/** このフレームのカメラのワールド → ビュー行列(SetFrameCamera で受け取る) */
			math::Matrix4x4 view_;

			/** パラメータ(メインスレッドで読み書きし、コマンド生成時に CB の値として写す) */
			bool  enabled_          = true;
			float focusDistance_    = 10.0f;
			float focusRange_       = 2.0f;
			float nearTransition_   = 5.0f;
			float farTransition_    = 5.0f;
			float maxRadius_        = 12.0f;
			bool  debugView_        = false;
			bool  autoFocusEnabled_ = false;
			float autoFocusSpeed_   = 0.1f;

			/**
			 * フォーカス RT を目標値(無ければ手動のピント)で書き直す要求。
			 * RT を作った直後・CommitResize・オートフォーカスの有効化で立て、Build でフォーカスのコマンドを積んだら下ろす
			 */
			bool focusResetPending_ = true;


		private:
			/** 出力 RT の生成(Initialize / PrepareResize 共用) */
			static RenderTargetHandle CreateOutputRT(const uint32_t width, const uint32_t height);
			/** CoC RT の生成(Initialize / PrepareResize 共用) */
			static RenderTargetHandle CreateCoCRT(const uint32_t width, const uint32_t height);
			/** オートフォーカスの資源の生成(Initialize で DoF 本体の生成が全部成功した後に呼ぶ。失敗したら作れた分を片付ける) */
			void InitializeFocus();


		public:
			DoFEffect() = default;
			~DoFEffect() override = default;


		public:
			bool Initialize(const uint32_t width, const uint32_t height) override;
			bool IsEnabled(const PostProcessContext& context) const override;

			/**
			 * リサイズ(設計書/エンジン課題解消設計.md P4)。ゲームスレッドで WaitIdle 済みの区間にだけ呼ぶ。
			 * PrepareResize は新寸法の出力 RT と CoC RT を一時領域へ作る(どちらかが失敗したら両方片付けて false)。
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
			 * メインパスのカメラを受け取り、CB に使うビュー行列を確定させる
			 * @param camera このフレームのカメラ
			 */
			void SetFrameCamera(const CameraData& camera);

			/** CoC RT(符号付きぼけ半径。Build を積んだフレームだけ中身が有効) */
			inline RenderTargetHandle GetCoCRT() const { return cocRTHandle_; }

			/** オートフォーカスの資源(シェーダー・CB・フォーカス RT)が揃っているか */
			inline bool IsAutoFocusAvailable() const { return focusShader_ && focusConstantBuffer_ && focusRTHandle_.IsValid(); }


			/**
			 * パラメータ
			 */
		public:
			/** 有効 / 無効(false なら入力を素通しする) */
			inline void SetEnabled(const bool enabled) { enabled_ = enabled; }
			inline bool GetEnabled() const { return enabled_; }

			/** ピント位置(ビュー空間の z。ワールド単位) */
			inline void  SetFocusDistance(const float distance) { focusDistance_ = distance < 0.0f ? 0.0f : distance; }
			inline float GetFocusDistance() const { return focusDistance_; }

			/** ピント位置の前後でぼけない幅(片側。ワールド単位) */
			inline void  SetFocusRange(const float range) { focusRange_ = range < 0.0f ? 0.0f : range; }
			inline float GetFocusRange() const { return focusRange_; }

			/** 手前側で CoC が 0 → -1 になる距離(ワールド単位。0 なら段差で切り替わる) */
			inline void  SetNearTransition(const float transition) { nearTransition_ = transition < 0.0f ? 0.0f : transition; }
			inline float GetNearTransition() const { return nearTransition_; }

			/** 奥側で CoC が 0 → +1 になる距離(ワールド単位。0 なら段差で切り替わる) */
			inline void  SetFarTransition(const float transition) { farTransition_ = transition < 0.0f ? 0.0f : transition; }
			inline float GetFarTransition() const { return farTransition_; }

			/** 最大のぼけ半径(画素。0 以下ならぼかさない) */
			inline void  SetMaxRadius(const float radius) { maxRadius_ = radius < 0.0f ? 0.0f : radius; }
			inline float GetMaxRadius() const { return maxRadius_; }

			/** デバッグ表示(ぼかしの代わりに CoC を色で出す。手前 = 赤、奥 = 青、ピント内 = 黒) */
			inline void SetDebugView(const bool debugView) { debugView_ = debugView; }
			inline bool GetDebugView() const { return debugView_; }

			/**
			 * オートフォーカスの有効 / 無効(有効なら画面中央の z にピントを寄せ、手動のピント位置は使わない)。
			 * 無効から有効にしたときは、前の値から寄せずに目標へ書き直す
			 */
			inline void SetAutoFocusEnabled(const bool enabled)
			{
				if (enabled && !autoFocusEnabled_) { focusResetPending_ = true; }
				autoFocusEnabled_ = enabled;
			}
			inline bool GetAutoFocusEnabled() const { return autoFocusEnabled_; }

			/** オートフォーカスが目標へ寄せる速さ(1 フレームあたりの割合。0〜1。1 なら即座に目標) */
			inline void  SetAutoFocusSpeed(const float speed) { autoFocusSpeed_ = speed < 0.0f ? 0.0f : (speed > 1.0f ? 1.0f : speed); }
			inline float GetAutoFocusSpeed() const { return autoFocusSpeed_; }
		};
	}
}
