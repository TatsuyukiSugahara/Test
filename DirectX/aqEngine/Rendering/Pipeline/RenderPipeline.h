#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include "IRenderPass.h"
#include "PassResources.h"


namespace aq
{
	namespace rendering
	{
		/**
		 * 分割画面の 1 ビュー矩形(ピクセル単位)。
		 * 元は Renderer::ViewRect にあったが、実行主体である RenderPipeline へ移した。
		 * Renderer.h 側は `using ViewRect = rendering::ViewRect;` で互換を保つ。
		 */
		struct ViewRect
		{
			float x = 0.0f;
			float y = 0.0f;
			float w = 0.0f;
			float h = 0.0f;
		};




		/**
		 * PipelineBuilder::Build() が確定させたパス列の実行主体(設計書/レンダーパイプライン設計.md §1.3)。
		 *
		 * Build() / BuildViews() はゲームスレッドから呼ばれ、コマンドを RenderCommandList に
		 * 積むだけ(ミューテックス不要。実行 (Execute) はレンダースレッド)。
		 */
		class RenderPipeline
		{
		private:
			std::vector<std::unique_ptr<IRenderPass>> passes_;
			PassResources                             resources_;

			/** GetOutputRT() が Output 未登録時に返す、直近の Build/BuildViews で使った Scene ハンドル */
			RenderTargetHandle lastSceneRT_;


		public:
			/** PipelineBuilder::Build() から構築される。列と検証済みの掲示板を受け取る */
			RenderPipeline(std::vector<std::unique_ptr<IRenderPass>> passes, PassResources resources);


		public:
			/** 1 ビュー分のコマンドを記録する。sceneRT は毎フレーム差し替える */
			void Build(RenderFrame& frame, RenderCommandList& outList,
			           const RenderTargetHandle sceneRT, const float viewportW, const float viewportH);

			/**
			 * 複数ビュー分のコマンドを記録する。
			 * Frame scope(先頭側) → View scope × ビュー数 → Frame scope(末尾側) の順に実行する。
			 */
			void BuildViews(RenderFrame* frames, const ViewRect* rects, const uint32_t viewCount,
			                RenderCommandList& outList, const RenderTargetHandle sceneRT,
			                const float viewportW, const float viewportH);

			/** Output キーが登録されていればそれ、無ければ直近の Build/BuildViews で使った Scene */
			RenderTargetHandle GetOutputRT() const;

			/** dynamic_cast で最初に見つかったパスを返す。無ければ nullptr */
			template <typename TPass>
			TPass* Find() const
			{
				for (const auto& pass : passes_) {
					if (auto* p = dynamic_cast<TPass*>(pass.get())) {
						return p;
					}
				}
				return nullptr;
			}

			/** 掲示板(パス間で RT ハンドルを受け渡す表)への参照 */
			inline PassResources& GetResources() { return resources_; }
		};
	}
}
