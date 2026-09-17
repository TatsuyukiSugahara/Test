#pragma once
#ifdef AQ_DEBUG_IMGUI
#include "UI/Font/TextStyle.h"
#include <string>
#include <string_view>

namespace aq
{
	namespace ui
	{
		// TextStyle アセット (.textstyle.json) を編集する部品。
		// 独立したデバッグパネルではなく、UIEditorDebugPanel が Text Inspector の
		// [Edit] ボタンから開くポップアップとして値で持つ (設計書 §10.5)。
		class TextStyleEditorPanel
		{
		public:
			void Open(std::string_view path);   // pathBuf_ へ設定し LoadStyle() してポップアップ表示を予約する
			void RenderPopup();                 // 毎フレーム呼ぶ。openRequested_ なら OpenPopup し、モーダルの中身を描く

		private:
			void RenderStyleList();
			void RenderStyleProperties();
			void RenderPreview();
			void SaveCurrent();
			void LoadStyle(const std::string& path);

			bool       openRequested_     = false;   // RenderPopup() で ImGui::OpenPopup するフラグ
			TextStyle  current_;
			char       pathBuf_[512]      = "Assets/Styles/Default.textstyle.json";
			char       statusMsg_[256]    = {};
			char       previewBuf_[256]   = u8"あいう ABC 123 !?";  // "あいう ABC 123 !?"
		};

	} // namespace ui
} // namespace aq
#endif
