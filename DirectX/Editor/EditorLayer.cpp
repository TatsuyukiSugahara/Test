#include "stdafx.h"
#include "EditorLayer.h"
#include <imgui/imgui.h>


namespace editor
{
	namespace
	{
		/** ゲームビューのウィンドウ名。ドッキング配置は editor_layout.ini にこの名前で保存される */
		static constexpr const char* GAME_VIEW_WINDOW_NAME = "Game View";
	}


	void EditorLayer::OnBeginFrame(ImGuiContext* gameContext)
	{
		gameViewInput_.BeginFrame(gameContext);
	}


	void EditorLayer::OnBuildUI(const aq::rendering::RenderTargetHandle gameViewRT)
	{
		// メインビューポート全体をドッキング先にする
		const ImGuiID dockspaceId = ImGui::DockSpaceOverViewport();

		// 配置の保存(editor_layout.ini)が無い初回だけ、ゲームビューをドックスペースいっぱいに置く
		ImGui::SetNextWindowDockID(dockspaceId, ImGuiCond_FirstUseEver);
		// 起動直後はゲームビューにフォーカスを与え、何もクリックしなくてもゲームを操作できるようにする(Game.exe と同じ始まり方)
		if (isFirstFrame_) {
			ImGui::SetNextWindowFocus();
			isFirstFrame_ = false;
		}
		const GameViewState gameViewState = DrawGameView(gameViewRT);

		// 次のフレームの入力の振り分けに使う
		gameViewInput_.SetViewState(gameViewState);
	}


	void EditorLayer::OnEndFrame()
	{
		ImGui::Render();
	}


	GameViewState EditorLayer::DrawGameView(const aq::rendering::RenderTargetHandle gameViewRT)
	{
		GameViewState state;
		// 入力用のボタンがアクティブか(エディタの別の項目をマウスで操作中かの判定から除くため)
		bool isInputButtonActive = false;

		// 絵を内側の余白なしで領域いっぱいに使う
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		const bool opened = ImGui::Begin(GAME_VIEW_WINDOW_NAME, nullptr,
		                                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		ImGui::PopStyleVar();

		if (opened)
		{
			const ImVec2 regionPos  = ImGui::GetCursorScreenPos();
			const ImVec2 regionSize = ImGui::GetContentRegionAvail();

			if (regionSize.x > 0.0f && regionSize.y > 0.0f)
			{
				// 余白(アスペクト比の差で空く部分)は黒にする
				ImGui::GetWindowDrawList()->AddRectFilled(
					regionPos, ImVec2(regionPos.x + regionSize.x, regionPos.y + regionSize.y), IM_COL32(0, 0, 0, 255));

				// ID はフレームをまたいで保持しない(RT の作り直しで無効になるため毎フレーム取り直す)
				void* textureId = aq::graphics::GraphicsDevice::Get().GetImGuiTextureId(gameViewRT);

				// displayRT はレンダー解像度で作られている(ゲームビューに合わせて作り直すことはしない)
				const float renderW = static_cast<float>(aq::Engine::Get().GetRenderWidth());
				const float renderH = static_cast<float>(aq::Engine::Get().GetRenderHeight());

				if (textureId == nullptr) {
					// ImGui のフォントは ASCII のみを読み込んでいるので英語で出す
					ImGui::TextUnformatted("Game View: not supported on this graphics API");
					ImGui::Text("(%s)", aq::graphics::GraphicsDevice::Get().GetBackendName());
				} else if (renderW > 0.0f && renderH > 0.0f) {
					// アスペクト比を保って領域に収まる最大の大きさにし、中央に置く
					const float scale = std::min(regionSize.x / renderW, regionSize.y / renderH);
					const ImVec2 imageSize(renderW * scale, renderH * scale);
					const ImVec2 imagePos(regionPos.x + (regionSize.x - imageSize.x) * 0.5f,
					                      regionPos.y + (regionSize.y - imageSize.y) * 0.5f);
					ImGui::SetCursorScreenPos(imagePos);
					ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(textureId)), imageSize);

					// 画像と同じ位置・大きさに見えないボタンを重ねる。押している間はこのボタンがアクティブになり、
					// 浮かせたゲームビューの中身をドラッグしてもウィンドウが動かない。
					// Image は ID を持たずホバーを取らない項目なので、重ねても当たり判定の取り合いにならない
					ImGui::SetCursorScreenPos(imagePos);
					ImGui::InvisibleButton("##GameViewInput", imageSize,
					                       ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
					isInputButtonActive = ImGui::IsItemActive();

					const ImVec2 itemMin = ImGui::GetItemRectMin();
					const ImVec2 itemMax = ImGui::GetItemRectMax();
					state.visible   = true;
					state.imageMinX = itemMin.x;
					state.imageMinY = itemMin.y;
					state.imageMaxX = itemMax.x;
					state.imageMaxY = itemMax.y;
				}
			}
		}

		// 画像を描かなかった(閉じている・畳まれている・未対応の API)ときはフォーカスも無い扱いにする
		state.focused = state.visible && ImGui::IsWindowFocused();
		ImGui::End();

		// マウスを押していない項目(アクティブな InputText など)は含めない。ゲームビューのクリックでフォーカスを移せなくなるため
		state.editorHoldsMouse = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)
		                      || (ImGui::IsAnyItemActive() && !isInputButtonActive && ImGui::IsAnyMouseDown());
		return state;
	}
}
