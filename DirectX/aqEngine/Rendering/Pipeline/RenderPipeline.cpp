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


		bool RenderPipeline::CanResize()
		{
			bool ok = true;
			for (const auto& pass : passes_)
			{
				const ResizePolicy policy = pass->GetResizePolicy();
				if (policy == ResizePolicy::Unsupported)
				{
					if (!resizeUnsupportedLogged_) {
						aq::StartupMarkf("[pipeline] リサイズ無効: %s が未対応(GetResizePolicy が Unsupported)", pass->GetName());
					}
					ok = false;
					continue;
				}
				if (policy == ResizePolicy::Independent)
				{
					// 掲示板へ登録するパスは、リサイズ後に登録をやり直す必要があるので Resizable でなければならない。
					PassDeclaration decl;
					pass->DeclareResources(decl);
					for (const PassResourceKey key : decl.GetWrites())
					{
						if (key != PassResourceKeys::Scene)
						{
							if (!resizeUnsupportedLogged_) {
								aq::StartupMarkf("[pipeline] リサイズ無効: Independent の %s が %s を Writes している(Resizable にする必要がある)",
								                 pass->GetName(), DescribePassKey(key));
							}
							ok = false;
							break;
						}
					}
				}
			}
			if (!ok) { resizeUnsupportedLogged_ = true; }
			return ok;
		}


		bool RenderPipeline::PrepareResize(const uint32_t width, const uint32_t height)
		{
			size_t prepared = 0;
			for (; prepared < passes_.size(); ++prepared)
			{
				auto& pass = passes_[prepared];
				if (pass->GetResizePolicy() != ResizePolicy::Resizable) { continue; }
				if (!pass->PrepareResize(width, height))
				{
					aq::StartupMarkf("[pipeline] PrepareResize 失敗: %s (%ux%u)", pass->GetName(), width, height);
					// 失敗したパス自身は自己清掃済み。成功済みのパスだけ取り消す。
					for (size_t i = 0; i < prepared; ++i) {
						if (passes_[i]->GetResizePolicy() == ResizePolicy::Resizable) { passes_[i]->AbortResize(); }
					}
					return false;
				}
			}
			return true;
		}


		void RenderPipeline::AbortResize()
		{
			for (auto& pass : passes_) {
				if (pass->GetResizePolicy() == ResizePolicy::Resizable) { pass->AbortResize(); }
			}
		}


		void RenderPipeline::CommitResize()
		{
			// 掲示板を Build 直後と同じ初期状態へ戻し、列順に登録し直す。
			// 旧 Output などが残っていると、Wire で「前段の出力」として旧 RT を拾ってしまう。
			resources_.Clear();
			resources_.Set(PassResourceKeys::Scene, lastSceneRT_);
			for (auto& pass : passes_) {
				if (pass->GetResizePolicy() == ResizePolicy::Resizable) { pass->CommitResize(resources_); }
			}
		}


		void RenderPipeline::Build(RenderFrame& frame, RenderCommandList& outList,
		                           const RenderTargetHandle sceneRT, const float viewportW, const float viewportH)
		{
			// 掲示板は毎フレーム消えないので、中継のキーは前フレームの値を消してから始める
			// (そのフレームのうちに書いた値だけを有効にする。設計書/被写界深度設計.md §0.2)。
			resources_.Remove(PassResourceKeys::PostInput);
			resources_.Set(PassResourceKeys::Scene, sceneRT);
			lastSceneRT_ = sceneRT;

			PassViewInfo view;
			view.viewIndex      = 0;
			view.viewCount      = 1;
			view.frameViewCount = 1;
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
			// 掲示板は毎フレーム消えないので、中継のキーは前フレームの値を消してから始める
			// (そのフレームのうちに書いた値だけを有効にする。設計書/被写界深度設計.md §0.2)。
			resources_.Remove(PassResourceKeys::PostInput);
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

			// 後段の viewCount は 1 のまま。分割画面かどうかは frameViewCount(全パス共通の実ビュー数)で見る。
			PassViewInfo fullView;
			fullView.viewIndex      = 0;
			fullView.viewCount      = 1;
			fullView.frameViewCount = viewCount;
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
				view.viewIndex      = v;
				view.viewCount      = viewCount;
				view.frameViewCount = viewCount;
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
