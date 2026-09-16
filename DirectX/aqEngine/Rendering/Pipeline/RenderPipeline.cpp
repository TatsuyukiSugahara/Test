#include "aq.h"
#include "RenderPipeline.h"


namespace aq
{
	namespace rendering
	{
		RenderPipeline::RenderPipeline(std::vector<std::unique_ptr<IRenderPass>> passes, PassResources resources)
			: passes_(std::move(passes))
			, resources_(std::move(resources))
			, lastSceneRT_()
		{
		}


		void RenderPipeline::Build(RenderFrame& frame, RenderCommandList& outList,
		                           const RenderTargetHandle sceneRT, const float viewportW, const float viewportH)
		{
			resources_.Set(PassResourceKeys::Scene, sceneRT);
			lastSceneRT_ = sceneRT;

			PassViewInfo view;
			view.viewIndex  = 0;
			view.viewCount  = 1;
			view.x = 0.0f;
			view.y = 0.0f;
			view.w = viewportW;
			view.h = viewportH;
			view.fullWidth  = viewportW;
			view.fullHeight = viewportH;

			for (auto& pass : passes_) {
				pass->Build(frame, view, resources_, outList);
			}
		}


		void RenderPipeline::BuildViews(RenderFrame* frames, const ViewRect* rects, const uint32_t viewCount,
		                                RenderCommandList& outList, const RenderTargetHandle sceneRT,
		                                const float viewportW, const float viewportH)
		{
			resources_.Set(PassResourceKeys::Scene, sceneRT);
			lastSceneRT_ = sceneRT;

			// 列を「先頭 〜 最初の View scope の手前(Frame scope 群)」「最初 〜 最後の View scope
			// (View scope 群)」「最後の View scope の後ろ 〜 末尾(Frame scope 群)」に分ける
			// (PipelineBuilder::Build が Frame* View* Frame* の並びを検証済み)。
			size_t firstView = passes_.size();
			size_t lastView  = 0;
			for (size_t i = 0; i < passes_.size(); ++i) {
				if (passes_[i]->GetScope() == PassScope::View) {
					if (firstView == passes_.size()) { firstView = i; }
					lastView = i;
				}
			}
			const bool   hasView = (firstView < passes_.size());
			const size_t preEnd  = hasView ? firstView       : passes_.size();
			const size_t postBeg = hasView ? (lastView + 1)  : passes_.size();

			PassViewInfo fullView;
			fullView.viewIndex  = 0;
			fullView.viewCount  = 1;
			fullView.x = 0.0f;
			fullView.y = 0.0f;
			fullView.w = viewportW;
			fullView.h = viewportH;
			fullView.fullWidth  = viewportW;
			fullView.fullHeight = viewportH;

			// 前段: Frame scope(影など)。frames[0] と全画面 view で 1 回だけ実行する。
			for (size_t i = 0; i < preEnd; ++i) {
				passes_[i]->Build(frames[0], fullView, resources_, outList);
			}

			// View scope: ビューごとにビューポートを切り替えて実行する。
			for (uint32_t v = 0; v < viewCount; ++v) {
				const ViewRect& rect = rects[v];
				outList.Enqueue<SetViewportCommand>(rect.x, rect.y, rect.w, rect.h);

				PassViewInfo view;
				view.viewIndex  = v;
				view.viewCount  = viewCount;
				view.x = rect.x;
				view.y = rect.y;
				view.w = rect.w;
				view.h = rect.h;
				view.fullWidth  = viewportW;
				view.fullHeight = viewportH;

				for (size_t i = firstView; hasView && i <= lastView; ++i) {
					passes_[i]->Build(frames[v], view, resources_, outList);
				}
			}

			// 全画面に戻す(後段の Frame scope はポストプロセスと UI で、どちらも画面全体を 1 回で処理する)。
			outList.Enqueue<SetViewportCommand>(0.0f, 0.0f, viewportW, viewportH);

			// 後段: Frame scope(ポストプロセス・UI)。frames[0] と全画面 view で 1 回だけ実行する。
			for (size_t i = postBeg; i < passes_.size(); ++i) {
				passes_[i]->Build(frames[0], fullView, resources_, outList);
			}
		}


		RenderTargetHandle RenderPipeline::GetOutputRT() const
		{
			if (resources_.Has(PassResourceKeys::Output)) {
				return resources_.Get(PassResourceKeys::Output);
			}
			return lastSceneRT_;
		}
	}
}
