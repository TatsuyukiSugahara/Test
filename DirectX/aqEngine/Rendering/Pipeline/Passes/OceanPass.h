#pragma once
#include "Rendering/Pipeline/IRenderPass.h"


namespace aq
{
	namespace rendering
	{
		/**
		 * 海パス(設計書/レンダーパイプライン設計.md §2)。
		 * フォワードパスの後・ポストプロセスの前に置く。FFT コンピュートに依存するため
		 * compute 非対応環境では除外される。
		 */
		class OceanPass final : public IRenderPass
		{
		public:
			OceanPass() = default;


			/**
			 * IRenderPass
			 */
		public:
			inline const char* GetName() const override { return "OceanPass"; }

			bool IsSupported() const override;
			void DeclareResources(PassDeclaration& decl) const override;
			void Build(RenderFrame& frame, const PassViewInfo& view,
			           PassResources& res, RenderCommandList& outList) override;
		};
	}
}
