#include "aq.h"
// ImGui 無効構成では空 TU にする。
#if defined(AQ_IMGUI)
#include "Platform/Common/ImGuiPointerInput.h"
#include "HID/Input.h"
#include <imgui/imgui.h>


namespace aq
{
	namespace platform
	{
		namespace ImGuiPointerInput
		{
			namespace
			{
				/** DeltaTime の代替値。初回フレームなど実測値が無いときに使う */
				static constexpr float FALLBACK_DELTA_TIME = 1.0f / 60.0f;

				/** ImGui の左ボタン番号(ImGuiMouseButton_Left) */
				static constexpr int32_t MOUSE_BUTTON_LEFT = 0;
			}


			bool Init()
			{
				EngineAssertMsg(ImGui::GetCurrentContext() != nullptr, "ImGui のコンテキストが未生成です");
				if (ImGui::GetCurrentContext() == nullptr) {
					return false;
				}

				ImGuiIO& io = ImGui::GetIO();
				io.BackendPlatformName = "aq_pointer";

				// BackendFlags は何も立てない。カーソル形状の変更(HasMouseCursors)も
				// 位置の設定(HasSetMousePos)もタッチでは満たせないため、
				// 対応を宣言すると ImGui 側が行う前提が崩れる。
				return true;
			}


			void Shutdown()
			{
				if (ImGui::GetCurrentContext() == nullptr) {
					return;
				}

				ImGui::GetIO().BackendPlatformName = nullptr;
			}


			void NewFrame()
			{
				ImGuiIO& io = ImGui::GetIO();

				// プラットフォームバックエンドが埋める 2 つ。
				//  - DisplaySize: 0 のままだと ImGui::NewFrame のサニティチェックで停止する
				//  - DeltaTime  : 0 以下だと同じくアサートに掛かる(初回フレームは実測値が無い)
				io.DisplaySize = ImVec2(static_cast<float>(aq::Engine::Get().GetScreenWidth()),
				                        static_cast<float>(aq::Engine::Get().GetScreenHeight()));
				const float deltaTime = aq::Engine::GetDeltaTime();
				io.DeltaTime = (deltaTime > 0.0f) ? deltaTime : FALLBACK_DELTA_TIME;

				// ポインタはマウス抽象から読む。タッチは TouchMouseBackend が
				// ここへ合成済みなので、タッチ固有の分岐は要らない。
				//
				// 自由関数(aq::hid::IsMousePressed 等)は使わない。あちらは
				// InputManager::IsMouseSuppressed() を見て false を返すが、抑制は
				// 「ImGui が使っている間ゲーム側を止める」ための仕組みであって、
				// ImGui 自身への入力を止めるものではない。ここで抑制に従うと
				// ImGui が入力を取った次のフレームから何も届かず、永久に触れなくなる。
				const aq::hid::Mouse* mouse = aq::hid::InputManager::Get().GetMousePtr();
				if (mouse == nullptr) {
					return;
				}

				const aq::math::Vector2 cursor = mouse->GetCursorPos();
				io.AddMousePosEvent(cursor.x, cursor.y);
				io.AddMouseButtonEvent(MOUSE_BUTTON_LEFT, mouse->IsPressed(aq::hid::MouseButton::Left));
			}
		}
	}
}
#endif // AQ_IMGUI
