#include "aq.h"
#include "UIPass.h"


namespace aq
{
	namespace rendering
	{
		UIPass::UIPass(std::function<void(RenderCommandList&)> callback)
			: callback_(std::move(callback))
		{
		}


		void UIPass::DeclareResources(PassDeclaration& decl) const
		{
			// UI は今どおりコールバック経由の描画で、PassResources のキーは読み書きしない。
			(void)decl;
		}


		void UIPass::Build(RenderFrame& frame, const PassViewInfo& view,
		                   PassResources& res, RenderCommandList& outList)
		{
			(void)frame;
			(void)view;
			(void)res;

			if (callback_) {
				callback_(outList);
			}
		}
	}
}
