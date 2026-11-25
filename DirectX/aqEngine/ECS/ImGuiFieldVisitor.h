#pragma once
#ifdef AQ_DEBUG_IMGUI
#include "Math/Vector.h"
#include "Math/Matrix.h"
#include "FieldAttr.h"
#include "Editor/AssetDragDrop.h"
#include <imgui/imgui.h>
#include <algorithm>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdio>
#include <string>

namespace aq
{
	namespace ecs
	{
		// フィールドのウィジェットの描き方（ImGuiFieldVisitor と ImGuiEditEventVisitor が共有する。設計 6.4 節 3）。
		// Draw* は「描いて、値が変わったら true」を返す。属性の readOnly（無効状態で描く）と tooltip もここで効かせる。
		// hidden は呼び出し側（Visitor）が見て、描かない。
		namespace fieldwidget
		{
			// 描いた直後の項目の状態（編集イベント Visitor が操作の開始・終了と項目の矩形を取るのに使う）。
			// ツールチップを出す前に取る（ツールチップのウィンドウが「最後の項目」を変えないように）。
			struct ItemInfo
			{
				bool   active = false;
				ImVec2 rectMin;
				ImVec2 rectMax;
			};


			inline void CaptureItem(ItemInfo* out)
			{
				if (!out) return;
				out->active  = ImGui::IsItemActive();
				out->rectMin = ImGui::GetItemRectMin();
				out->rectMax = ImGui::GetItemRectMax();
			}


			// readOnly の項目にもツールチップを出すため AllowWhenDisabled を付ける。
			inline void ShowTooltip(const char* text)
			{
				if (text && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled))
					ImGui::SetTooltip("%s", text);
			}


			// 範囲の渡し方: ImGui のドラッグは v_min < v_max のときだけ範囲を効かせるので、
			// 片側だけなら無い側を ±FLT_MAX にする。範囲が無いときは今と同じく 0, 0（既存の操作感を変えない）。
			inline bool HasRange(const FieldAttr& attr)
			{
				return attr.hasMin || attr.hasMax;
			}

			inline float FloatMin(const FieldAttr& attr)
			{
				if (!HasRange(attr)) return 0.0f;
				return attr.hasMin ? attr.min : -FLT_MAX;
			}

			inline float FloatMax(const FieldAttr& attr)
			{
				if (!HasRange(attr)) return 0.0f;
				return attr.hasMax ? attr.max : FLT_MAX;
			}

			inline int IntMin(const FieldAttr& attr)
			{
				if (!HasRange(attr)) return 0;
				return attr.hasMin ? static_cast<int>(attr.min) : INT_MIN;
			}

			inline int IntMax(const FieldAttr& attr)
			{
				if (!HasRange(attr)) return 0;
				return attr.hasMax ? static_cast<int>(attr.max) : INT_MAX;
			}

			// 範囲があるときは Ctrl+クリックの直接入力でも範囲の外に出さない。
			inline ImGuiSliderFlags RangeFlags(const FieldAttr& attr)
			{
				return HasRange(attr) ? ImGuiSliderFlags_AlwaysClamp : ImGuiSliderFlags_None;
			}

			inline float SpeedOr(const FieldAttr& attr, const float defaultSpeed)
			{
				return attr.speed > 0.0f ? attr.speed : defaultSpeed;
			}


			// YXZ 回転順 (Ry→Rx→Rz) で Euler 角(度)を抽出して Vector3 で返す
			inline aq::math::Vector3 QuaternionToEulerDeg(const aq::math::Quaternion& q)
			{
				aq::math::Matrix4x4 mat;
				mat.MakeRotationFromQuaternion(q);

				const float sinPitch = -mat.m[2][1];
				const float pitch    = asinf(std::clamp(sinPitch, -1.0f, 1.0f));
				float yaw, roll;
				if (fabsf(sinPitch) < 0.9999f)
				{
					yaw  = atan2f(mat.m[2][0], mat.m[2][2]);
					roll = atan2f(mat.m[0][1], mat.m[1][1]);
				}
				else
				{
					// ジンバルロック：yaw に回転を集約、roll を 0 に固定
					yaw  = atan2f(-mat.m[1][0], mat.m[0][0]);
					roll = 0.0f;
				}
				constexpr float RAD_TO_DEG = 180.0f / 3.14159265358979f;
				return aq::math::Vector3(pitch * RAD_TO_DEG, yaw * RAD_TO_DEG, roll * RAD_TO_DEG);
			}


			// Euler 角(度)の Vector3(pitch, yaw, roll) から Quaternion を生成する
			inline aq::math::Quaternion EulerDegToQuaternion(const aq::math::Vector3& deg)
			{
				constexpr float DEG_TO_RAD = 3.14159265358979f / 180.0f;
				aq::math::Quaternion result;
				DirectX::XMStoreFloat4(&result.vector,
					DirectX::XMQuaternionNormalize(
						DirectX::XMQuaternionRotationRollPitchYaw(
							deg.x * DEG_TO_RAD,
							deg.y * DEG_TO_RAD,
							deg.z * DEG_TO_RAD)));
				return result;
			}


			inline bool DrawFloat(const char* label, float& value, const FieldAttr& attr, ItemInfo* item = nullptr)
			{
				ImGui::BeginDisabled(attr.readOnly);
				const bool changed = ImGui::DragFloat(label, &value, SpeedOr(attr, 0.01f),
					FloatMin(attr), FloatMax(attr), "%.3f", RangeFlags(attr));
				CaptureItem(item);
				ShowTooltip(attr.tooltip);
				ImGui::EndDisabled();
				return changed;
			}


			inline bool DrawInt(const char* label, int& value, const FieldAttr& attr, ItemInfo* item = nullptr)
			{
				ImGui::BeginDisabled(attr.readOnly);
				const bool changed = ImGui::DragInt(label, &value, SpeedOr(attr, 1.0f),
					IntMin(attr), IntMax(attr), "%d", RangeFlags(attr));
				CaptureItem(item);
				ShowTooltip(attr.tooltip);
				ImGui::EndDisabled();
				return changed;
			}


			inline bool DrawBool(const char* label, bool& value, const FieldAttr& attr, ItemInfo* item = nullptr)
			{
				ImGui::BeginDisabled(attr.readOnly);
				const bool changed = ImGui::Checkbox(label, &value);
				CaptureItem(item);
				ShowTooltip(attr.tooltip);
				ImGui::EndDisabled();
				return changed;
			}


			inline bool DrawVector3(const char* label, aq::math::Vector3& value, const FieldAttr& attr, ItemInfo* item = nullptr)
			{
				ImGui::BeginDisabled(attr.readOnly);
				float f[3] = { value.x, value.y, value.z };
				const bool changed = ImGui::DragFloat3(label, f, SpeedOr(attr, 0.01f),
					FloatMin(attr), FloatMax(attr), "%.3f", RangeFlags(attr));
				if (changed)
				{
					value.x = f[0]; value.y = f[1]; value.z = f[2];
				}
				CaptureItem(item);
				ShowTooltip(attr.tooltip);
				ImGui::EndDisabled();
				return changed;
			}


			// Quaternion は Euler 角(度)に変換して表示・編集する。順序は YXZ。範囲は度に対して効く。
			inline bool DrawQuaternion(const char* label, aq::math::Quaternion& value, const FieldAttr& attr, ItemInfo* item = nullptr)
			{
				ImGui::BeginDisabled(attr.readOnly);
				const aq::math::Vector3 euler = QuaternionToEulerDeg(value);
				float f[3] = { euler.x, euler.y, euler.z };
				const bool changed = ImGui::DragFloat3(label, f, SpeedOr(attr, 0.5f),
					FloatMin(attr), FloatMax(attr), "%.3f", RangeFlags(attr));
				if (changed)
					value = EulerDegToQuaternion(aq::math::Vector3(f[0], f[1], f[2]));
				CaptureItem(item);
				ShowTooltip(attr.tooltip);
				ImGui::EndDisabled();
				return changed;
			}


			// 文字列入力（パスと、パス以外の文字列で共有）。Enter で確定し、かつ値が変わったときだけ
			// value を書き換えて true を返す（同じ値の Enter では false。設計 6.4 節 3）。
			// 編集中はウィジェット内部バッファが保持するため、毎フレーム value を渡しても問題ない。
			inline bool DrawString(const char* label, std::string& value, const FieldAttr& attr, ItemInfo* item = nullptr)
			{
				ImGui::BeginDisabled(attr.readOnly);
				char buf[512];
				// snprintf は切り詰めても必ず NUL 終端する。
				snprintf(buf, sizeof(buf), "%s", value.c_str());
				const bool committed = ImGui::InputText(
					label, buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue);
				CaptureItem(item);

				// アセットのパスのフィールドは、コンテンツブラウザからのドラッグのドロップ先にする(設計 7.10 節 3)。
				// readOnly のときは付けない。受けたパスが今の値と違うときだけ、Enter で確定したのと同じ扱いにする。
				std::string dropped;
				bool        hasDropped = false;
				if (attr.asset != AssetKind::None && !attr.readOnly && ImGui::BeginDragDropTarget())
				{
					hasDropped = aq::editor::AcceptAssetDragPayload(attr.asset, dropped) && dropped != value;
					ImGui::EndDragDropTarget();
				}

				if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled))
				{
					// 既存の「Press Enter to apply」に、属性のツールチップを改行して続ける。
					if (attr.tooltip)
						ImGui::SetTooltip("Press Enter to apply\n%s", attr.tooltip);
					else
						ImGui::SetTooltip("Press Enter to apply");
				}
				ImGui::EndDisabled();

				if (hasDropped)
				{
					value = dropped;
					return true;
				}

				if (!committed || value == buf) return false;
				value = buf;
				return true;
			}


			inline void DrawReadOnly(const char* label, const aq::math::Vector3& value)
			{
				ImGui::Text("%-22s (%.2f, %.2f, %.2f)", label, value.x, value.y, value.z);
			}

			inline void DrawReadOnly(const char* label, const aq::math::Quaternion& value)
			{
				const aq::math::Vector3 euler = QuaternionToEulerDeg(value);
				ImGui::Text("%-22s (%.1f, %.1f, %.1f) deg", label, euler.x, euler.y, euler.z);
			}

			inline void DrawReadOnly(const char* label, const char* value)
			{
				ImGui::Text("%-22s %s", label, value ? value : "(none)");
			}

			inline void DrawReadOnly(const char* label, const int value)
			{
				ImGui::Text("%-22s %d", label, value);
			}
		}




		// Inspect<V> パターン用の ImGui 実装（対象を直接書き換える）。
		// Field()    = 編集可能フィールド
		// ReadOnly() = 表示専用フィールド
		// changed フラグは少なくとも 1 つのフィールドが変更されたとき true になる。
		// 末尾の attr（フィールド属性）の範囲・速さ・readOnly・hidden・tooltip を効かせる。
		struct ImGuiFieldVisitor
		{
			bool changed = false;

			// Field の第1引数は永続キー（persistKey）。第3引数 displayLabel が指定されれば
			// それを UI 表示に使う（省略時は persistKey をそのまま表示）。
			// これにより JSON キーと UI 文言を分離できる（2 引数呼び出しは従来どおり persistKey 表示）。
			static const char* Label(const char* persistKey, const char* displayLabel)
			{
				return displayLabel ? displayLabel : persistKey;
			}

			void Field(const char* persistKey, float& value, const char* displayLabel = nullptr, const FieldAttr& attr = {})
			{
				if (attr.hidden) return;
				if (fieldwidget::DrawFloat(Label(persistKey, displayLabel), value, attr))
					changed = true;
			}

			void Field(const char* persistKey, int& value, const char* displayLabel = nullptr, const FieldAttr& attr = {})
			{
				if (attr.hidden) return;
				if (fieldwidget::DrawInt(Label(persistKey, displayLabel), value, attr))
					changed = true;
			}

			void Field(const char* persistKey, aq::math::Vector3& value, const char* displayLabel = nullptr, const FieldAttr& attr = {})
			{
				if (attr.hidden) return;
				if (fieldwidget::DrawVector3(Label(persistKey, displayLabel), value, attr))
					changed = true;
			}

			// Quaternion は Euler 角(度)に変換して表示・編集する。順序は YXZ。
			void Field(const char* persistKey, aq::math::Quaternion& value, const char* displayLabel = nullptr, const FieldAttr& attr = {})
			{
				if (attr.hidden) return;
				if (fieldwidget::DrawQuaternion(Label(persistKey, displayLabel), value, attr))
					changed = true;
			}

			void Field(const char* persistKey, bool& value, const char* displayLabel = nullptr, const FieldAttr& attr = {})
			{
				if (attr.hidden) return;
				if (fieldwidget::DrawBool(Label(persistKey, displayLabel), value, attr))
					changed = true;
			}

			// パス以外の文字列。動きは FieldPath と同じ（Enter で確定し、値が変わったときだけ書き換える）。
			void Field(const char* persistKey, std::string& value, const char* displayLabel = nullptr, const FieldAttr& attr = {})
			{
				if (attr.hidden) return;
				if (fieldwidget::DrawString(Label(persistKey, displayLabel), value, attr))
					changed = true;
			}

			// ファイルパス入力。Enter で確定し、かつ値が変わったときだけ path を更新して true を返す。
			bool FieldPath(const char* persistKey, std::string& path, const char* displayLabel = nullptr, const FieldAttr& attr = {})
			{
				if (attr.hidden) return false;
				// 従来どおり changed は立てない（戻り値で呼び出し側がロード副作用を起こす）。
				return fieldwidget::DrawString(Label(persistKey, displayLabel), path, attr);
			}

			void ReadOnly(const char* label, const aq::math::Vector3& value)    { fieldwidget::DrawReadOnly(label, value); }
			void ReadOnly(const char* label, const aq::math::Quaternion& value) { fieldwidget::DrawReadOnly(label, value); }
			void ReadOnly(const char* label, const char* value)                 { fieldwidget::DrawReadOnly(label, value); }
			void ReadOnly(const char* label, int value)                         { fieldwidget::DrawReadOnly(label, value); }
		};
	}
}
#endif
