#pragma once
#include "Rendering/Pipeline/IRenderPass.h"


namespace aq
{
	namespace rendering
	{
		/**
		 * パーティクルパス(設計書/レンダーパイプライン設計.md §2)。
		 * 半透明ビルボードを描く。エミッタ共有の動的 VB への多重書き込みを避けるため、
		 * 先頭ビューでのみ描く(既知の制限。01_レンダリング設計.md §5)。
		 */
		class ParticlePass final : public IRenderPass
		{
		public:
			ParticlePass() = default;


			/**
			 * IRenderPass
			 */
		public:
			inline const char* GetName() const override { return "ParticlePass"; }

			void DeclareResources(PassDeclaration& decl) const override;
			void Build(RenderFrame& frame, const PassViewInfo& view,
			           PassResources& res, RenderCommandList& outList) override;
		};
	}
}
