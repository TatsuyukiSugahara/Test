#pragma once
#ifdef AQ_DEBUG_IMGUI
#include "Core/IDebugRenderable.h"
#include "UI/Font/TextStyle.h"
#include <string>

namespace aq
{
	namespace ui
	{
		// TextStyle アセット (.textstyle.json) を作成・編集するエディタパネル。
		// UIEditorDebugPanel と同様に DebugUI::Get().Register() で登録して使う。
		class TextStyleEditorPanel : public IDebugRenderable
		{
		public:
			void DebugRenderMenu() override;
			void DebugRender()     override;

		private:
			void RenderStyleList();
			void RenderStyleProperties();
			void RenderPreview();
			void SaveCurrent();
			void LoadStyle(const std::string& path);

			bool       show_              = false;
			TextStyle  current_;
			char       pathBuf_[512]      = "Assets/Styles/Default.textstyle.json";
			char       statusMsg_[256]    = {};
			char       previewBuf_[256]   = u8"あいう ABC 123 !?";  // "あいう ABC 123 !?"
		};

	} // namespace ui
} // namespace aq
#endif
