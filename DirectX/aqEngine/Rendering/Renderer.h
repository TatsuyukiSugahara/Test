#pragma once
#include <memory>
#include <functional>
#include "RenderFrame.h"
#include "RenderCommandList.h"
#include "RenderTargetHandle.h"
#include "Shadow/IShadowRenderer.h"
#include "PostProcess/IPostProcessRenderer.h"
#include "Deferred/IDeferredRenderer.h"


namespace aq
{
	namespace graphics { class RenderContext; }

	namespace rendering
	{
		/**
		 * RenderFrame を RenderCommandList に変換する（記録フェーズ担当）。
		 *
		 * 非同期パス（推奨）:
		 *   BuildCommandList() でコマンドを記録し、RenderThread::Submit() で実行する。
		 *
		 * 同期パス（デバッグ専用）:
		 *   RenderDebugSync() は BuildCommandList() + 即時 Execute() を 1 呼び出しで行う。
		 *   リリースビルドでは除外されるため、プロダクションコードから呼んではならない。
		 */
		class Renderer
		{
		public:
			/**
			 * シャドウレンダラーを設定する。nullptr を渡すと影なしになる。
			 * mainRTHandle / mainViewportW / mainViewportH は RenderDebugSync 専用
			 * （デバッグ同期パスでメイン RT に描画する際に使う）。
			 */
			void SetShadowRenderer(std::unique_ptr<IShadowRenderer> sr,
			                       RenderTargetHandle mainRTHandle,
			                       float mainViewportW, float mainViewportH);

			IShadowRenderer* GetShadowRenderer() const { return shadowRenderer_.get(); }

			void SetPostProcessRenderer(std::unique_ptr<IPostProcessRenderer> pp);
			IPostProcessRenderer* GetPostProcessRenderer() const { return postProcessRenderer_.get(); }

			/** ディファードレンダラーを設定する。nullptr を渡すとフォワードのみになる。 */
			void SetDeferredRenderer(std::unique_ptr<IDeferredRenderer> dr);
			IDeferredRenderer* GetDeferredRenderer() const { return deferredRenderer_.get(); }

			/**
			 * UI 描画コールバックを設定する。
			 * BuildCommandList() のポストプロセス後・ImGui 前に呼ばれる。
			 * Renderer が UI に直接依存しないよう、std::function で疎結合にする。
			 * Application::OnInitialize() 等で UIContext::GetBatchRenderer() と接続する。
			 */
			void SetUIRenderCallback(std::function<void(RenderCommandList&)> cb)
			{
				uiRenderCallback_ = std::move(cb);
			}

			/**
			 * Hi-Z ピラミッド構築コールバックを設定する。
			 * G-Buffer パスの直後 (worldPos 確定後) に呼ばれる。
			 * Renderer が Occlusion 層に直接依存しないよう std::function で疎結合にする。
			 */
			void SetHiZBuildCallback(std::function<void(const RenderFrame&, RenderCommandList&)> cb)
			{
				hiZBuildCallback_ = std::move(cb);
			}

			/**
			 * ポストプロセスが設定されている場合はその最終出力 RT を、そうでなければ sceneRT を返す。
			 * RenderThread::Submit の displayRT に渡す値を決めるために使う。
			 */
			RenderTargetHandle GetDisplayRTHandle(RenderTargetHandle sceneRT) const;

			/**
			 * ゲームスレッドでフレームデータを outList に記録する。
			 * シャドウレンダラーが設定されている場合は frame.shadow を自動的に埋める。
			 * rtHandle / viewportW / viewportH はシャドウパス後に復元するRTとビューポートを指定する。
			 */
			void BuildCommandList(RenderFrame& frame, RenderCommandList& outList,
			                      RenderTargetHandle rtHandle,
			                      float viewportW, float viewportH,
			                      bool applyPostProcess = true) const;

#if _DEBUG
			/**
			 * 同期実行（デバッグ専用）。BuildCommandList + Execute を 1 呼び出しで行う。
			 * 必ずレンダースレッドから呼ぶこと。メインスレッドから呼ぶと
			 * D3D11 immediate context の単一スレッド規則に違反する。
			 */
			void RenderDebugSync(graphics::RenderContext& context, RenderFrame& frame);
#endif

		private:
			void RecordDrawItem(const RenderItem&  item,
			                    const CameraData&  camera,
			                    RenderCommandList& outList) const;

			std::unique_ptr<IShadowRenderer>        shadowRenderer_;
			std::unique_ptr<IPostProcessRenderer>   postProcessRenderer_;
			std::unique_ptr<IDeferredRenderer>      deferredRenderer_;
			std::function<void(RenderCommandList&)> uiRenderCallback_;
			std::function<void(const RenderFrame&, RenderCommandList&)> hiZBuildCallback_;
			RenderTargetHandle                      mainRTHandle_;
			float                                   mainViewportW_ = 0.f;
			float                                   mainViewportH_ = 0.f;
		};
	}
}
