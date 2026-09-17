#pragma once
#ifdef AQ_DEBUG_IMGUI
#include "Core/IDebugRenderable.h"
#include "UI/UITypes.h"
#include "UI/Animation/UIAnimatedProperty.h"
#include <string>
#include <unordered_map>

struct ImDrawList;

namespace aq
{
	namespace ui
	{
		class UIObject;
		struct UIAnimationClip;
		struct UIAnimationTrack;

		// ImGui ベースの UIAnimation Timeline エディタ。
		// DebugUI::Get().Register() で登録するとメニューから開ける。
		// 対象 UIObject は UIEditorSession の選択(UI Editor の Hierarchy)を参照するだけで、
		// 独自の Object Picker や保存先は持たない。
		// クリップは Clip -> Track -> Keyframe の 3 階層 (ClipTrack は廃止済み)。
		class UIAnimationEditor : public IDebugRenderable
		{
		public:
			void DebugRenderMenu() override;
			void DebugRender()     override;
			const char* GetDebugCategory() const override { return "UI"; }

		private:
			// ---- Left panel ----
			void DrawClipPanel(UIObject* obj);
			void DrawTrackList(UIAnimationClip& clip);

			// ---- Timeline (right panel) ----
			void DrawTimeline(UIObject* obj, UIAnimationClip& clip);
			void DrawRuler(ImDrawList* dl, float ox, float oy, float w, float duration);
			void DrawTrackRow(ImDrawList* dl, UIAnimationClip& clip,
			                  int trackIdx,
			                  float ox, float rowY, float rowH, float duration);

			// ---- Bottom panels ----
			void DrawKeyframeInspector(UIAnimationClip& clip);
			void DrawPreviewControls(UIObject* obj, UIAnimationClip& clip, float dt);

			// ---- Preview ----
			void TakeSnapshot(UIObject* obj, const UIAnimationClip& clip);
			void RestoreSnapshot(UIObject* obj);
			void ApplyScrub(UIObject* obj, const UIAnimationClip& clip);

			// ---- Selection ----
			void OnTargetChanged(UIObject* prevObj);   // 選択オブジェクトが変わった時の後始末

			// ---- State ----
			bool           show_               = false;
			bool           windowPinned_       = false;
			UIObjectHandle prevTargetHandle_;   // UIEditorSession の選択変化検出用

			// 選択 (名前ではなくインデックス)。selClipIdx_ が変わったときだけ
			// clipNameBuf_ / clipParamBuf_ / clipGroupBuf_ を同期する (prevSelClipIdx_ で検出)
			int            selClipIdx_         = -1;
			int            prevSelClipIdx_     = -1;
			int            selTrackIdx_        = -1;
			int            selKeyframeIdx_     = -1;

			// Timeline 表示
			float          zoomPxPerSec_       = 150.f;
			float          timelineScrollX_    = 0.f;

			// Drag / context-menu state
			bool           isDraggingKf_       = false;
			int            dragKfTrackIdx_     = -1;
			int            dragKfKiIdx_        = -1;
			float          ctxClickTime_       = 0.f;

			// Preview
			float          scrubTime_          = 0.f;
			bool           isPlaying_          = false;
			float          playSpeed_          = 1.f;
			bool           hasSnapshot_        = false;
			std::unordered_map<UIAnimatedProperty, float> snapshot_;

			// 選択中クリップの Name / Param / Group 用バッファ (ImGui InputText 用)。
			// 全トラック共有の static バッファは持たない
			char           clipNameBuf_[64]    = {};
			char           clipParamBuf_[64]   = {};
			char           clipGroupBuf_[64]   = {};
		};

	} // namespace ui
} // namespace aq
#endif
