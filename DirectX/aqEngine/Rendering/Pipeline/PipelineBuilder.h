#pragma once
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>
#include "IRenderPass.h"


namespace aq
{
	namespace rendering
	{
		class RenderPipeline;


		/**
		 * IRenderPass の列を組み立て、検証して RenderPipeline を確定させる(設計書/レンダーパイプライン設計.md §1.3)。
		 *
		 * P1 では Add のみを提供する。InsertAfter / InsertBefore / Replace / Remove は P2 で追加する。
		 */
		class PipelineBuilder
		{
		private:
			std::vector<std::unique_ptr<IRenderPass>> passes_;


		public:
			/** パスを生成して末尾へ追加する(引数はパスのコンストラクタへそのまま転送する) */
			template <typename TPass, typename... Args>
			PipelineBuilder& Add(Args&&... args)
			{
				passes_.push_back(std::make_unique<TPass>(std::forward<Args>(args)...));
				return *this;
			}

			/** 生成済みのパスを末尾へ追加する */
			PipelineBuilder& Add(std::unique_ptr<IRenderPass> pass);

			/**
			 * 列を検証して確定する。
			 *  1. IsSupported() が false のパスを除外する(ログのみ)
			 *  2. Reads が「初期状態(Scene)+ それまでの Writes」に無ければログを出す(P1 は失敗させない)
			 *  3. Frame scope が View scope の列の途中に挟まっていればログを出す(P1 は失敗させない)
			 *  4. 各パスの Setup を順に呼ぶ。false を返したらログを出して nullptr を返す
			 *  5. 確定した列をログに 1 行出す
			 */
			std::unique_ptr<RenderPipeline> Build(const uint32_t width, const uint32_t height);
		};
	}
}
