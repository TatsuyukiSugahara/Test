#pragma once
#ifdef AQ_DEBUG_IMGUI
#include "Core/IDebugRenderable.h"
#include "UI/UITypes.h"
#include "Graphics/IShaderResourceView.h"
#include <memory>
#include <vector>
#include <string>

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
		// 選択・保存先・未保存フラグは UIEditorSession を通じて
		// Animation Editor と共有する。
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
			void RenderToolbar(UIObject* root);
			void RenderTextOverlay();   // UITextComponent の内容を ImGui でスクリーンに仮描画
			void ClearForReload();      // Reload 実行直前にエディタ側の状態を捨てる

			bool            show_              = false;
			bool            showTextOverlay_   = false;  // テキスト仮描画オーバーレイ (SDF 未整備時のみ使用)
			UIObjectHandle  prevSelectedHandle_;   // バッファ同期タイミング検出用
			char            nameBuf_[128]      = {};

			// ロードしたテクスチャ SRV を生存保持
			std::vector<std::shared_ptr<graphics::IShaderResourceView>> loadedTextures_;

			// テクスチャパス入力バッファ (選択変更時にコンポーネントの texturePath から同期)
			char imageTexPathBuf_[256]       = {};
			char nineSliceTexPathBuf_[256]   = {};
			char circleGaugeTexPathBuf_[256] = {};

			// Text 入力バッファ (選択変更時にコンポーネントの content / textStylePath から同期)
			char textContentBuf_[512]        = {};
			char textStyleBuf_[512]          = {};

			// 保存関連
			char saveAsPathBuf_[512] = {};  // Save As ポップアップ用パス入力
			char statusMsg_[256]     = {};  // 保存結果メッセージ
		};
	}
}
#endif
