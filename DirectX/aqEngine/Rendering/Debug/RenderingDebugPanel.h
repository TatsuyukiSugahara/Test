#pragma once
#ifdef AQ_DEBUG_IMGUI
#include "Core/IDebugRenderable.h"
#include <vector>
#include <memory>

namespace aq
{
	namespace rendering
	{
		/**
		 * レンダリング系デバッグパネルをタブで束ねる集約パネル。
		 * AddTab() で IDebugRenderable を登録するだけでタブが増える。
		 * 子パネルの所有権を持つ場合は owned に追加する。
		 */
		class RenderingDebugPanel : public IDebugRenderable
		{
		public:
			struct Tab
			{
				const char*      label;
				IDebugRenderable* panel;
			};

			void AddTab(const char* label, IDebugRenderable* panel);
			void TakeOwnership(std::unique_ptr<IDebugRenderable> panel);

			void DebugRenderMenu() override;
			void DebugRender()     override;
			const char* GetDebugCategory() const override { return "Rendering"; }

		private:
			std::vector<Tab>                              tabs_;
			std::vector<std::unique_ptr<IDebugRenderable>> owned_;
			bool                                          show_ = false;
		};
	}
}
#endif
