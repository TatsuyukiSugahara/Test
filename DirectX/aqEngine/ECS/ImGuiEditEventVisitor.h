#pragma once
#ifdef AQ_DEBUG_IMGUI
#include "ImGuiFieldVisitor.h"
#include "JsonFieldVisitor.h"
#include "FieldAttr.h"
#include "Util/SimpleJson.h"
#include <imgui/imgui.h>
#include <string>
#include <utility>
#include <vector>

namespace aq
{
	namespace ecs
	{
		/**
		 * フィールドの編集イベントの段階
		 */
		enum class FieldEditPhase : uint8_t
		{
			Begin,   // 操作の開始（変更前の値）
			Update,  // 操作中に値が変わった（現在の値）
			Commit,  // 操作の確定（変更後の値）
			Cancel,  // 値を変えずに操作が終わった（値は持たない）
		};




		/**
		 * フィールドの編集イベント
		 *
		 * 値は JsonWriteVisitor と同じ JSON 表現（ToJson）。Quaternion は UI では Euler 角で編集するが、
		 * 値は [x,y,z,w] になる。
		 */
		struct FieldEditEvent
		{
			FieldEditPhase  phase = FieldEditPhase::Begin;
			/** Reflect の永続キー */
			std::string     key;
			/** Begin = 変更前の値、Update = 現在の値、Commit = 変更後の値、Cancel = 空 */
			util::JsonValue value;
		};




		/**
		 * フィールドごとの項目の矩形（検査用。recordItemRects が true のときだけ残す）
		 */
		struct FieldItemRect
		{
			std::string key;
			ImVec2      min;
			ImVec2      max;
		};




		/**
		 * 編集イベント Visitor（設計 エディタ全体設計.md 6.4 節 3）
		 *
		 * 編集用の写しに対してウィジェットを描き、フィールド単位の「操作」ごとに
		 * Begin / Update / Commit / Cancel のイベントを、キーと JSON 値の組で events に積む。
		 * 書き換えるのは渡された写しだけ。写しの持ち方（項目が操作中の間は編集モデルから作り直さない）、
		 * Begin と Commit を組にしたコマンド作り、描かなくなった操作の取り消しは呼び出し側の責務。
		 *
		 * 呼び出し側は、描く前に編集対象を区別する ID（ノード ID と typeName など）を PushID する。
		 * 操作の状態はフレームをまたぐので、Visitor の実体ではなく ImGui のウィンドウの状態保存
		 * （GetStateStorage）にフィールドの ID で持つ。ImGui のメインスレッドからだけ呼ぶこと。
		 */
		struct ImGuiEditEventVisitor
		{
			/** このフレームに起きたイベント（呼び出し側が毎フレーム読む） */
			std::vector<FieldEditEvent> events;

			/** 検査用: true ならフィールドごとの項目の矩形を itemRects に残す */
			bool                        recordItemRects = false;
			std::vector<FieldItemRect>  itemRects;


		public:
			void Field(const char* persistKey, float& value, const char* displayLabel = nullptr, const FieldAttr& attr = {})
			{
				Operate(persistKey, value, attr, [&](float& v, fieldwidget::ItemInfo* item)
					{
						return fieldwidget::DrawFloat(ImGuiFieldVisitor::Label(persistKey, displayLabel), v, attr, item);
					});
			}

			void Field(const char* persistKey, int& value, const char* displayLabel = nullptr, const FieldAttr& attr = {})
			{
				Operate(persistKey, value, attr, [&](int& v, fieldwidget::ItemInfo* item)
					{
						return fieldwidget::DrawInt(ImGuiFieldVisitor::Label(persistKey, displayLabel), v, attr, item);
					});
			}

			void Field(const char* persistKey, bool& value, const char* displayLabel = nullptr, const FieldAttr& attr = {})
			{
				Operate(persistKey, value, attr, [&](bool& v, fieldwidget::ItemInfo* item)
					{
						return fieldwidget::DrawBool(ImGuiFieldVisitor::Label(persistKey, displayLabel), v, attr, item);
					});
			}

			void Field(const char* persistKey, aq::math::Vector3& value, const char* displayLabel = nullptr, const FieldAttr& attr = {})
			{
				Operate(persistKey, value, attr, [&](aq::math::Vector3& v, fieldwidget::ItemInfo* item)
					{
						return fieldwidget::DrawVector3(ImGuiFieldVisitor::Label(persistKey, displayLabel), v, attr, item);
					});
			}

			void Field(const char* persistKey, aq::math::Quaternion& value, const char* displayLabel = nullptr, const FieldAttr& attr = {})
			{
				Operate(persistKey, value, attr, [&](aq::math::Quaternion& v, fieldwidget::ItemInfo* item)
					{
						return fieldwidget::DrawQuaternion(ImGuiFieldVisitor::Label(persistKey, displayLabel), v, attr, item);
					});
			}

			// パス以外の文字列。確定し、かつ値が変わったときだけ Begin と Commit を同じフレームに出す。
			void Field(const char* persistKey, std::string& value, const char* displayLabel = nullptr, const FieldAttr& attr = {})
			{
				CommitOnEnter(persistKey, value, displayLabel, attr);
			}

			// パス。戻り値は「確定し、かつ値が変わった」ときだけ true（ImGuiFieldVisitor と同じ）。
			bool FieldPath(const char* persistKey, std::string& path, const char* displayLabel = nullptr, const FieldAttr& attr = {})
			{
				return CommitOnEnter(persistKey, path, displayLabel, attr);
			}

			// 表示だけ。イベントは出さない。
			void ReadOnly(const char* label, const aq::math::Vector3& value)    { fieldwidget::DrawReadOnly(label, value); }
			void ReadOnly(const char* label, const aq::math::Quaternion& value) { fieldwidget::DrawReadOnly(label, value); }
			void ReadOnly(const char* label, const char* value)                 { fieldwidget::DrawReadOnly(label, value); }
			void ReadOnly(const char* label, int value)                         { fieldwidget::DrawReadOnly(label, value); }


		private:
			/** 状態保存に置く操作の状態のビット */
			static constexpr int STATE_ACTIVE  = 1 << 0;   // 操作中
			static constexpr int STATE_CHANGED = 1 << 1;   // 操作中に値が変わった


			void Push(const FieldEditPhase phase, const char* key, util::JsonValue value)
			{
				FieldEditEvent ev;
				ev.phase = phase;
				ev.key   = key;
				ev.value = std::move(value);
				events.push_back(std::move(ev));
			}


			void RecordRect(const char* key, const fieldwidget::ItemInfo& item)
			{
				if (!recordItemRects) return;
				itemRects.push_back(FieldItemRect{ key, item.rectMin, item.rectMax });
			}


			/**
			 * ドラッグ・チェックボックスなどの「操作」でイベントを出す
			 *
			 * 操作の状態（操作中か、操作中に値が変わったか、最後に描いたフレーム番号）は
			 * GetStateStorage にフィールドの ID（GetID(キー) の下の 2 つの ID）で持つ。
			 * 変わったかどうかは描き方の共有関数の戻り値で判定する（IsItemEdited は使わない。
			 * EndGroup は、同じフレームで先に別の項目が変わっていると DragFloat3 の変更の印を引き継がないため）。
			 */
			template <typename T, typename DrawFn>
			void Operate(const char* key, T& value, const FieldAttr& attr, DrawFn&& draw)
			{
				if (attr.hidden) return;

				// 描く前に値を控える（JSON にするのはイベントを出すときだけ）
				const T before = value;

				fieldwidget::ItemInfo item;
				const bool changed = draw(value, &item);
				RecordRect(key, item);

				// readOnly は無効状態で描くので操作にならない
				if (attr.readOnly) return;

				ImGuiStorage* storage = ImGui::GetStateStorage();
				ImGui::PushID(key);
				const ImGuiID stateId = ImGui::GetID("##aqEditState");
				const ImGuiID frameId = ImGui::GetID("##aqEditFrame");
				ImGui::PopID();

				const int frame     = ImGui::GetFrameCount();
				int       state     = storage->GetInt(stateId, 0);
				const int lastFrame = storage->GetInt(frameId, -1);

				// 前のフレームに描かれていない操作は古い操作の残り。イベントを出さずに状態を消す
				// （描かれなくなった操作の取り消しは呼び出し側が行う）
				if (state != 0 && lastFrame != frame - 1) state = 0;

				// 操作中になった
				if (!(state & STATE_ACTIVE) && item.active)
				{
					Push(FieldEditPhase::Begin, key, ToJson(before));
					state = STATE_ACTIVE;
				}

				if (changed)
				{
					if (state & STATE_ACTIVE)
					{
						Push(FieldEditPhase::Update, key, ToJson(value));
						state |= STATE_CHANGED;
					}
					else
					{
						// 操作中でないのに値が変わった（直接入力の途中で描かれなくなった文字列を
						// ImGui が再表示のときに適用した、など）。同じフレームで 1 つの操作として閉じる
						Push(FieldEditPhase::Begin, key, ToJson(before));
						Push(FieldEditPhase::Commit, key, ToJson(value));
					}
				}

				// 操作中でなくなった
				if ((state & STATE_ACTIVE) && !item.active)
				{
					if (state & STATE_CHANGED)
						Push(FieldEditPhase::Commit, key, ToJson(value));
					else
						Push(FieldEditPhase::Cancel, key, util::JsonValue());
					state = 0;
				}

				storage->SetInt(stateId, state);
				storage->SetInt(frameId, frame);
			}


			/** パスと文字列: 確定し、かつ値が控えと違うときだけ Begin と Commit を同じフレームに出す */
			bool CommitOnEnter(const char* key, std::string& value, const char* displayLabel, const FieldAttr& attr)
			{
				if (attr.hidden) return false;

				const std::string before = value;

				fieldwidget::ItemInfo item;
				const bool committed = fieldwidget::DrawString(ImGuiFieldVisitor::Label(key, displayLabel), value, attr, &item);
				RecordRect(key, item);

				if (!committed) return false;
				Push(FieldEditPhase::Begin, key, ToJson(before));
				Push(FieldEditPhase::Commit, key, ToJson(value));
				return true;
			}
		};
	}
}
#endif
