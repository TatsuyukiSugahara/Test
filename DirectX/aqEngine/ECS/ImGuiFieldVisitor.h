#pragma once
#ifdef AQ_DEBUG_IMGUI
#include "Math/Vector.h"
#include "Math/Matrix.h"
#include <imgui/imgui.h>
#include <algorithm>
#include <cmath>
#include <string>

namespace aq
{
	namespace ecs
	{
		// Inspect<V> パターン用の ImGui 実装。
		// Field()    = 編集可能フィールド
		// ReadOnly() = 表示専用フィールド
		// changed フラグは少なくとも 1 つのフィールドが変更されたとき true になる。
		struct ImGuiFieldVisitor
		{
			bool changed = false;

			void Field(const char* label, float& value)
			{
				if (ImGui::DragFloat(label, &value, 0.01f))
					changed = true;
			}

			void Field(const char* label, aq::math::Vector3& value)
			{
				float f[3] = { value.x, value.y, value.z };
				if (ImGui::DragFloat3(label, f, 0.01f))
				{
					value.x = f[0]; value.y = f[1]; value.z = f[2];
					changed = true;
				}
			}

			// Quaternion は Euler 角(度)に変換して表示・編集する。順序は YXZ。
			void Field(const char* label, aq::math::Quaternion& value)
			{
				aq::math::Vector3 euler = QuaternionToEulerDeg(value);
				float f[3] = { euler.x, euler.y, euler.z };
				if (ImGui::DragFloat3(label, f, 0.5f))
				{
					value   = EulerDegToQuaternion(aq::math::Vector3(f[0], f[1], f[2]));
					changed = true;
				}
			}

			void Field(const char* label, bool& value)
			{
				if (ImGui::Checkbox(label, &value))
					changed = true;
			}

			// ファイルパス入力。Enter キーで path を更新して true を返す。
			// 編集中はウィジェット内部バッファが保持するため、毎フレーム path を渡しても問題ない。
			bool FieldPath(const char* label, std::string& path)
			{
				char buf[512];
				strncpy_s(buf, sizeof(buf), path.c_str(), _TRUNCATE);
				const bool committed = ImGui::InputText(
					label, buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue);
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
					ImGui::SetTooltip("Enter キーで適用");
				if (committed)
					path = buf;
				return committed;
			}

			void ReadOnly(const char* label, const aq::math::Vector3& value)
			{
				ImGui::Text("%-22s (%.2f, %.2f, %.2f)", label, value.x, value.y, value.z);
			}

			void ReadOnly(const char* label, const aq::math::Quaternion& value)
			{
				const aq::math::Vector3 euler = QuaternionToEulerDeg(value);
				ImGui::Text("%-22s (%.1f, %.1f, %.1f) deg", label, euler.x, euler.y, euler.z);
			}

			void ReadOnly(const char* label, const char* value)
			{
				ImGui::Text("%-22s %s", label, value ? value : "(none)");
			}

			void ReadOnly(const char* label, int value)
			{
				ImGui::Text("%-22s %d", label, value);
			}

		private:
			// YXZ 回転順 (Ry→Rx→Rz) で Euler 角(度)を抽出して Vector3 で返す
			static aq::math::Vector3 QuaternionToEulerDeg(const aq::math::Quaternion& q)
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
			static aq::math::Quaternion EulerDegToQuaternion(const aq::math::Vector3& deg)
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
		};
	}
}
#endif
