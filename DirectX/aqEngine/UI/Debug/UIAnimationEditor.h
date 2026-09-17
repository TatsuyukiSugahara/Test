#pragma once
#ifdef AQ_DEBUG_IMGUI
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
		class UIAnimationComponent;
		struct UIAnimationClip;
		struct UIAnimationTrack;

		// UIAnimation の Clip / Track / Keyframe を編集する部品。
		// 独立ウィンドウは持たず、UIEditorDebugPanel (UI Editor) が Inspector の
		// Animation タブと下部 Timeline パネルの中身として呼び出す (設計書 §10、P3)。
		// 対象 UIObject は呼び出し側が渡す。選択の元は UIEditorSession (UI Editor の
		// Hierarchy) であり、このクラス自身は Object Picker や保存先を持たない。
		// クリップは Clip -> Track -> Keyframe の 3 階層 (ClipTrack は廃止済み)。
		class UIAnimationEditor
		{
		private:
			// 選択 (名前ではなくインデックス)。selClipIdx_ が変わったときだけ
			// clipNameBuf_ / clipParamBuf_ / clipGroupBuf_ を同期する (prevSelClipIdx_ で検出)
			int            selClipIdx_         = -1;
			int            prevSelClipIdx_     = -1;
			int            selTrackIdx_        = -1;
			int            selKeyframeIdx_     = -1;

			// Play when Combo で Advanced を選んだ直後、Advanced セクションを開かせるフラグ
			bool           wantOpenAdvanced_   = false;

			/** Timeline 表示 */
			float          zoomPxPerSec_       = 150.f;

			/** Drag / context-menu state */
			bool           isDraggingKf_       = false;
			int            dragKfTrackIdx_     = -1;
			int            dragKfKiIdx_        = -1;
			float          ctxClickTime_       = 0.f;

			/** Preview (スクラブによる擬似再生。ランタイムには触らずスナップショットへ直書きする) */
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


			/**
			 * 公開 API (設計書 §10、P3)。UIEditorDebugPanel から呼ばれる。署名は固定
			 */
		public:
			// Inspector の Animation タブの中身 (Clip 一覧 + 選択 Clip の設定 + Advanced + 検証エラー)
			void DrawAnimationTab(UIObject* obj);

			// 下部パネルの中身 (Timeline + Keyframe Inspector + Preview)。呼び出し側が BeginChild 済み
			void DrawTimelinePanel(UIObject* obj, float dt);

			// 選択が変わった / 選択オブジェクトが消えたときの後始末 (prevObj は null の場合あり)
			void OnTargetChanged(UIObject* prevObj);

			// Reload 直前。選択・スナップショット・プレビュー状態を全部捨てる (オブジェクトには触らない)
			void Reset();

			// root 以下の全 UIAnimationComponent を ValidateDetailed にかけ、
			// "<ObjectName>: <message>" の形で errors に積む。0 件なら true
			static bool ValidateTree(const UIObject* root, std::vector<std::string>& errors);


			/**
			 * Animation タブの内部
			 */
		private:
			void SyncClipBuffers(const UIAnimationClip& clip);
			void ApplyPlayWhenSelection(UIAnimationComponent* anim, const int clipIndex, UIAnimationClip& clip, const int option);


			/**
			 * Timeline: 左パネル (Clip 一覧) / トラック編集
			 */
		private:
			void DrawTimelineClipList(UIObject* obj, UIAnimationComponent* anim);
			void DrawTrackList(UIAnimationClip& clip);


			/**
			 * Timeline: 右パネル
			 */
		private:
			void DrawTimeline(UIObject* obj, UIAnimationClip& clip);
			void DrawRuler(ImDrawList* dl, float ox, float oy, float w, float duration);
			void DrawTrackRow(ImDrawList* dl, UIAnimationComponent* anim, UIAnimationClip& clip,
			                  int trackIdx,
			                  float ox, float rowY, float rowH, float duration);


			/**
			 * 下部パネル
			 */
		private:
			void DrawKeyframeInspector(UIAnimationClip& clip);
			void DrawPreviewControls(UIObject* obj, UIAnimationComponent* anim, UIAnimationClip& clip, float dt);


			/**
			 * Preview (スクラブ用スナップショット)
			 */
		private:
			void TakeSnapshot(UIObject* obj, const UIAnimationClip& clip);
			void RestoreSnapshot(UIObject* obj);
			void ApplyScrub(UIObject* obj, const UIAnimationClip& clip);
		};

	} // namespace ui
} // namespace aq
#endif
