/**
 * プロファイラ表示用 ImGui パネル
 *
 * - スレッドごとにスコープ階層 (親→子) を時間付きツリーで表示する。
 * - "Stop" ボタンで表示を固定し、その時点のフレームをじっくり確認できる。
 *   "Resume" で毎フレーム更新に戻る。
 */
#pragma once
#ifdef AQ_DEBUG_IMGUI
#include <vector>
#include "Core/IDebugRenderable.h"
#include "Util/Profiler.h"

namespace aq
{
	namespace profile
	{
		class ProfilerDebugPanel : public IDebugRenderable
		{
		public:
			void DebugRenderMenu() override;
			void DebugRender()     override;
			void RenderContent()   override;
			const char* GetDebugLabel() const override { return "Profiler"; }
			const char* GetDebugCategory() const override { return "Profiling"; }

		private:
			// タイムライン (横=時間 / 縦=スレッド) 表示
			void RenderTimeline();
			// 折りたたみ詳細 (ツリー) 表示
			void RenderThreadTree(const ThreadFrame& frame);
			void RenderNode(const ThreadFrame& frame,
			                const std::vector<std::vector<int>>& children,
			                int index, double threadTotalMs);

			std::vector<ThreadFrame> snapshot_;
			double                   snapshotFrameMs_ = 0.0;  // snapshot_ と同時に凍結する 1F 時間
			bool                     show_   = false;
			bool                     paused_ = false;
			float                    zoom_   = 1.0f;  // タイムライン横方向ズーム
		};
	}
}
#endif
