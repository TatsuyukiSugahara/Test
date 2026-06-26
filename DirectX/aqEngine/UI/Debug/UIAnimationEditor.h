#pragma once
#ifdef AQ_DEBUG_IMGUI
#include "Core/IDebugRenderable.h"
#include "UI/UITypes.h"
#include "UI/Animation/UIAnimatedProperty.h"
#include <string>
#include <unordered_map>
#include <vector>

struct ImDrawList;

namespace aq
{
	namespace ui
	{
		class UIObject;
		class UIAnimationClip;
		struct UIClipTrack;
		struct UIAnimationTrack;

		// ImGui ベースの UIAnimation Timeline エディタ。
		// DebugUI::Get().Register() で登録するとメニューから開ける。
		// 対象 UIObject を選択し、クリップ / トラック / キーフレームを
		// タイムライン上で編集・保存できる。
		class UIAnimationEditor : public IDebugRenderable
		{
		public:
			void DebugRenderMenu() override;
			void DebugRender()     override;

		private:
			// ---- Object picker ----
			void DrawObjectPicker();
			void CollectObjects(UIObject* obj, int depth);

			// ---- Left panel ----
			void DrawClipPanel(UIObject* obj);
			void DrawClipTrackTree(UIAnimationClip& clip);

			// ---- Timeline (right panel) ----
			void DrawTimeline(UIObject* obj, UIAnimationClip& clip);
			void DrawRuler(ImDrawList* dl, float ox, float oy, float w, float duration);
			void DrawTrackRow(ImDrawList* dl, UIAnimationClip& clip,
			                  int ctIdx, int ptIdx,
			                  float ox, float rowY, float rowH, float duration);

			// ---- Bottom panels ----
			void DrawKeyframeInspector(UIAnimationClip& clip);
			void DrawPreviewControls(UIObject* obj, UIAnimationClip& clip, float dt);

			// ---- Preview ----
			void TakeSnapshot(UIObject* obj, const UIAnimationClip& clip);
			void RestoreSnapshot(UIObject* obj);
			void ApplyScrub(UIObject* obj, const UIAnimationClip& clip);

			// ---- Save / Load ----
			void DrawSaveLoad(UIObject* obj);

			// ---- State ----
			bool           show_               = false;
			UIObjectHandle targetHandle_;

			// 選択
			std::string    selectedClip_;
			int            selClipTrackIdx_    = -1;
			int            selPropTrackIdx_    = -1;
			int            selKeyframeIdx_     = -1;

			// Timeline 表示
			float          zoomPxPerSec_       = 150.f;
			float          timelineScrollX_    = 0.f;

			// Preview
			float          scrubTime_          = 0.f;
			bool           isPlaying_          = false;
			float          playSpeed_          = 1.f;
			bool           hasSnapshot_        = false;
			std::unordered_map<UIAnimatedProperty, float> snapshot_;

			// 名前バッファ (ImGui InputText 用)
			char           clipNameBuf_[64]    = {};
			char           ctNameBuf_[64]      = {};
			char           condParamBuf_[64]   = {};
			char           savePathBuf_[512]   = {};
			char           statusMsg_[256]     = {};

			// Object picker キャッシュ
			struct ObjEntry { UIObjectHandle handle; std::string displayName; };
			std::vector<ObjEntry> objList_;
			char           objFilterBuf_[64]   = {};
			bool           objPickerOpen_      = false;
		};

	} // namespace ui
} // namespace aq
#endif
