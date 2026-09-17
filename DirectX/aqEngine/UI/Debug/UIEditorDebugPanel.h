#pragma once
#ifdef AQ_DEBUG_IMGUI
#include "Core/IDebugRenderable.h"
#include "UI/UITypes.h"
#include "UI/Debug/UIAnimationEditor.h"
#include "UI/Debug/TextStyleEditorPanel.h"
#include "Graphics/IShaderResourceView.h"
#include <initializer_list>
#include <memory>
#include <vector>
#include <string>

namespace aq
{
	namespace ui
	{
		class UIObject;
		class UITransformComponent;

		// ============================================================
		// UIEditorDebugPanel — ImGui による UIObject ツリーのリアルタイム編集
		//
		// DebugUI::Get().Register(panel.get()) で登録すると
		// 現在の最前面 UIScreen のオブジェクト階層を表示し、
		// 選択した UIObject の Transform / Image / Canvas プロパティを編集、
		// UIObject の追加・削除・コンポーネント追加ができる。
		// 選択・保存先・未保存フラグは UIEditorSession を通じて共有する。
		// Inspector は [Properties] / [Animation] のタブに分かれ、Animation タブは
		// UIAnimationEditor を組み込み部品として呼び出す(独立パネルではない)。
		// Animation タブ選択中は下部に Timeline を出す。TextStyle の編集は
		// Text Inspector の [Edit] から開くポップアップとして TextStyleEditorPanel を呼ぶ。
		// ============================================================
		class UIEditorDebugPanel : public IDebugRenderable
		{
		public:
			void DebugRenderMenu() override;
			void DebugRender()     override;
			const char* GetDebugCategory() const override { return "UI"; }

		private:
			// Inspector が今どちらのタブを表示しているか (下部 Timeline の表示判定に使う)
			enum class InspectorTab { Properties, Animation };

			void RenderTree(UIObject* node);
			void RenderProperties(UIObject* obj);
			void RenderPropertiesTab(UIObject* obj);   // Inspector の Properties タブの中身

			// 録画対象ウィジェットの直後に呼ぶ。編集されていたら dirty を立て、
			// オートキー録画中なら props のキーをスクラブ時刻へ入れる。
			// 対応する UIAnimatedProperty が無いウィジェットは MarkDirtyIfEdited() のまま
			void MarkEdited(UIObject* obj, std::initializer_list<UIAnimatedProperty> props);

			void RenderAnchorPicker(UITransformComponent* tc);
			void RenderToolbar(UIObject* root);
			void RenderTextOverlay();   // UITextComponent の内容を ImGui でスクリーンに仮描画
			void ClearForReload();      // Reload 実行直前にエディタ側の状態を捨てる

			bool            show_              = false;
			bool            showTextOverlay_   = false;  // テキスト仮描画オーバーレイ (SDF 未整備時のみ使用)
			UIObjectHandle  prevSelectedHandle_;   // バッファ同期タイミング検出用
			InspectorTab    inspectorTab_      = InspectorTab::Properties;
			char            nameBuf_[128]      = {};

			// Inspector の Animation タブ / 下部 Timeline に組み込む部品 (設計書 §11 P3)
			UIAnimationEditor    animationEditor_;
			// Text Inspector の [Edit] から開く TextStyle 編集ポップアップ (設計書 §10.5)
			TextStyleEditorPanel textStyleEditor_;

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
