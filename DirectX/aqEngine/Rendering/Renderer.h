#pragma once
#include <memory>
#include "RenderFrame.h"
#include "RenderCommandList.h"
#include "RenderTargetHandle.h"
#include "Pipeline/RenderPipeline.h"
#include "Shadow/IShadowRenderer.h"
#include "PostProcess/IPostProcessRenderer.h"
#include "Deferred/IDeferredRenderer.h"
#include "Sky/SkyRenderer.h"


namespace aq
{
	namespace graphics { class RenderContext; }

	namespace rendering
	{
		/**
		 * RenderPipeline を保持し、RenderFrame から RenderCommandList を組み立てる（記録フェーズ担当）。
		 *
		 * パスの並び自体は Renderer が持たない。ゲームが PipelineBuilder で組んだ
		 * （または PipelinePresets::Standard() が組んだ）RenderPipeline を SetPipeline() で丸ごと
		 * 受け取り、BuildCommandList / BuildCommandListViews はそこへ委譲するだけになる
		 * (設計書/レンダーパイプライン設計.md)。
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
			/** 分割画面のビュー矩形 (ピクセル単位)。実体は Pipeline/RenderPipeline.h の rendering::ViewRect */
			using ViewRect = rendering::ViewRect;


		public:
			/**
			 * 確定済みのパイプラインを設定する。
			 * mainRTHandle / mainViewportW / mainViewportH は RenderDebugSync 専用
			 * （デバッグ同期パスでメイン RT に描画する際に使う）。
			 */
			void SetPipeline(std::unique_ptr<RenderPipeline> pipeline,
			                 RenderTargetHandle mainRTHandle,
			                 float mainViewportW, float mainViewportH);

			/** 設定済みのパイプライン。ゲームが Find<T>() でパスへアクセスする入口 */
			RenderPipeline* GetPipeline() const { return pipeline_.get(); }

			/**
			 * パイプラインの Output キーが登録されていればその RT、無ければ直近に使った Scene RT。
			 * RenderThread::Submit の displayRT に渡す値を決めるために使う。
			 */
			RenderTargetHandle GetOutputRT() const;

			// ---- 互換アクセサ ----
			// AquaDash (Game/Application/Application.cpp) が使っている。
			// P1〜P2 の間は pipeline_->Find<T>() へ転送する形で残し、P5 で削除する
			// (設計書/レンダーパイプライン設計.md §2)。
			IShadowRenderer*      GetShadowRenderer() const;
			IPostProcessRenderer* GetPostProcessRenderer() const;
			IDeferredRenderer*    GetDeferredRenderer() const;
			SkyRenderer*          GetSkyRenderer() const;

			/**
			 * ゲームスレッドでフレームデータを outList に記録する。RenderPipeline::Build() への委譲。
			 * rtHandle / viewportW / viewportH は今フレームのシーン RT とビューポート。
			 */
			void BuildCommandList(RenderFrame& frame, RenderCommandList& outList,
			                      RenderTargetHandle rtHandle,
			                      float viewportW, float viewportH) const;

			/**
			 * 分割画面用: 複数ビュー (カメラ毎に構築済みの RenderFrame + ビューポート矩形) を
			 * 1 本のコマンドリストへ記録する。RenderPipeline::BuildViews() への委譲。
			 * ビュー数 1 の分岐は設けない (その場合は従来の BuildCommandList を使うこと)。
			 */
			void BuildCommandListViews(RenderFrame* frames, const ViewRect* rects, const uint32_t viewCount,
			                           RenderCommandList& outList, RenderTargetHandle rtHandle,
			                           float viewportW, float viewportH) const;

#if _DEBUG
			/**
			 * 同期実行（デバッグ専用）。BuildCommandList + Execute を 1 呼び出しで行う。
			 * 必ずレンダースレッドから呼ぶこと。メインスレッドから呼ぶと
			 * D3D11 immediate context の単一スレッド規則に違反する。
			 */
			void RenderDebugSync(graphics::RenderContext& context, RenderFrame& frame);
#endif

		private:
			std::unique_ptr<RenderPipeline> pipeline_;
			RenderTargetHandle              mainRTHandle_;
			float                           mainViewportW_ = 0.0f;
			float                           mainViewportH_ = 0.0f;
		};
	}
}
