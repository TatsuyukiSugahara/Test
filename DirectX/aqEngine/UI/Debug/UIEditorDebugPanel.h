#pragma once
#ifdef AQ_DEBUG_IMGUI
#include "Core/IDebugRenderable.h"
#include "UI/UITypes.h"
#include "Graphics/IShaderResourceView.h"
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>

namespace aq
{
	namespace ui
	{
		class UIObject;

		// ============================================================
		// UIEditorDebugPanel — ImGui による UIObject ツリーのリアルタイム編集
		//
		// DebugUI::Get().Register(panel.get()) で登録すると
		// 現在の最前面 UIScreen のオブジェクト階層を表示し、
		// 選択した UIObject の Transform / Image / Canvas プロパティを編集、
		// UIObject の追加・削除・コンポーネント追加ができる。
		// ============================================================
		class UIEditorDebugPanel : public IDebugRenderable
		{
		public:
			void DebugRenderMenu() override;
			void DebugRender()     override;
			const char* GetDebugCategory() const override { return "UI"; }

		private:
			void RenderTree(UIObject* node);
			void RenderProperties(UIObject* obj);
			void RenderAnchorPicker(struct UITransformComponent* tc);
			void RenderSaveLoad(UIObject* root);
			void RenderTextOverlay();   // UITextComponent の内容を ImGui でスクリーンに仮描画

			bool            show_              = false;
			bool            showTextOverlay_   = false;  // テキスト仮描画オーバーレイ (SDF 未整備時のみ使用)
			UIObjectHandle  selectedHandle_;
			UIObjectHandle  prevSelectedHandle_;   // 名前バッファ更新タイミング検出用
			char            nameBuf_[128]      = {};

			// ロードしたテクスチャ SRV を生存保持
			std::vector<std::shared_ptr<graphics::IShaderResourceView>> loadedTextures_;
			// オブジェクト ID ごとのテクスチャパス (選択変更時に texPathBuf_ へ同期)
			std::unordered_map<UIObjectID, std::string> texturePaths_;
			char texPathBuf_[256] = {};

			// JSON 保存/ロード用パスバッファ
			char savePathBuf_[512] = {};
			char statusMsg_[256]   = {};  // 保存/ロード結果メッセージ
		};
	}
}
#endif
