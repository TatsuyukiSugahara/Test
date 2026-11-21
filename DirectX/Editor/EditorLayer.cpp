#include "stdafx.h"
#include "EditorLayer.h"
#include "Core/AppHost.h"
#include "Level/LevelEditModel.h"
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>   // DockBuilder(初期配置の関数だけで使う)


namespace editor
{
	namespace
	{
		/** ゲームビューのウィンドウ名。ドッキング配置は editor_layout.ini にこの名前で保存される */
		static constexpr const char* GAME_VIEW_WINDOW_NAME = "Game View";

		/** インスペクターのウィンドウ名 */
		static constexpr const char* INSPECTOR_WINDOW_NAME = "Inspector";

		/** 初期配置の幅の割合(ドックスペース全体に対して) */
		static constexpr float HIERARCHY_WIDTH_RATIO = 0.20f;
		static constexpr float INSPECTOR_WIDTH_RATIO = 0.25f;

		/** ゲームビューへ追従させるレンダー解像度の下限(これより小さい領域では切り上げ、絵に余白が出る) */
		static constexpr uint32_t MIN_GAME_VIEW_RENDER_SIZE = 64;
	}


	void EditorLayer::OnBeginFrame(ImGuiContext* gameContext)
	{
		// 編集コマンドの実行と再生・停止の遷移はここ(FlushRender の後・ECS の反復の外)でだけ行う。
		// コマンド → 遷移の順にする(同じフレームに再生も要求されていたら、編集を反映してから再生する)。
		// 自己テストは遷移の前に進めて要求を出す。
		// ゲームの開始・終了処理やプレビューの作り直しは、ランタイムと同じくゲーム用コンテキストを current にして呼ぶ
		{
			ImGuiContext* editorContext = ImGui::GetCurrentContext();
			if (gameContext != nullptr) {
				ImGui::SetCurrentContext(gameContext);
			}

			editorContext_.ApplyPending();

			if (startSelfTestOnLaunch_) {
				startSelfTestOnLaunch_ = false;
				playStopSelfTest_.Start(true);
			}
			playStopSelfTest_.Tick(playModeController_);
			playModeController_.ApplyPendingRequest();

			ImGui::SetCurrentContext(editorContext);
		}

		gameViewInput_.BeginFrame(gameContext);
	}


	void EditorLayer::OnBuildUI(const aq::rendering::RenderTargetHandle gameViewRT)
	{
		// メニューバーはドックスペースより先に置く(ドッキング先がメニューバーの下の領域になる)
		DrawMainMenuBar();
		// File メニューのモーダルと Ctrl+S(メニューバーの外の ID の階層で開く)
		levelFileMenu_.DrawPopups(editorContext_);

		// メインビューポート全体をドッキング先にする
		const ImGuiID dockspaceId = ImGui::DockSpaceOverViewport();
		const bool isLayoutBuilt = SetupInitialLayout(dockspaceId);

		// パネルはゲームビューより先に描く(現れたウィンドウはフォーカスを取るので、最初のフォーカスをゲームビューに残すため)
		hierarchyPanel_.Draw(editorContext_);
		DrawInspector();

		// 配置の保存(editor_layout.ini)が無い初回だけ、ゲームビューをドックスペースいっぱいに置く
		// (初期配置で分けたときは、分けた中央に置いてあるので指定しない)
		if (!isLayoutBuilt) {
			ImGui::SetNextWindowDockID(dockspaceId, ImGuiCond_FirstUseEver);
		}
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
				// レンダー解像度をゲームビューの大きさに追従させる(設計書/エディタ全体設計.md 5.9 節 E3b-2)。
				// 毎フレーム渡してよい(同じ値なら何も起きない。実際の作り直しは 2 フレーム続いてから)。
				// 小さすぎる解像度は作り直しが割に合わないので 64 で止める(そのときだけ余白が出る)。
				{
					const uint32_t overrideW = std::max<uint32_t>(static_cast<uint32_t>(regionSize.x), MIN_GAME_VIEW_RENDER_SIZE);
					const uint32_t overrideH = std::max<uint32_t>(static_cast<uint32_t>(regionSize.y), MIN_GAME_VIEW_RENDER_SIZE);
					aq::Engine::Get().SetRenderSizeOverride(overrideW, overrideH);
				}

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


	void EditorLayer::DrawMainMenuBar()
	{
		if (!ImGui::BeginMainMenuBar()) return;

		levelFileMenu_.DrawMenu(editorContext_);
		ImGui::Separator();
		DrawPlayToolbar();
		levelFileMenu_.DrawStatus(editorContext_);

		ImGui::EndMainMenuBar();
	}


	void EditorLayer::DrawPlayToolbar()
	{
		const aq::RunMode mode = aq::AppHost::Get().GetRunMode();
		// 自己テストの実行中と、要求が実行待ちの間は操作を受け付けない(遷移はフレームの先頭で 1 つずつ行う)
		const bool isBusy = playStopSelfTest_.IsRunning() || playModeController_.HasPendingRequest();

		// ImGui のフォントは ASCII のみを読み込んでいるので英語で出す
		ImGui::BeginDisabled(isBusy || mode != aq::RunMode::Edit);
		if (ImGui::Button("Play")) {
			playModeController_.RequestPlay();
		}
		ImGui::EndDisabled();

		// Pause と Resume は同じ位置に置き、モードで切り替える
		if (mode == aq::RunMode::Pause) {
			ImGui::BeginDisabled(isBusy);
			if (ImGui::Button("Resume")) {
				playModeController_.RequestResume();
			}
			ImGui::EndDisabled();
		} else {
			ImGui::BeginDisabled(isBusy || mode != aq::RunMode::Play);
			if (ImGui::Button("Pause")) {
				playModeController_.RequestPause();
			}
			ImGui::EndDisabled();
		}

		ImGui::BeginDisabled(isBusy || mode == aq::RunMode::Edit);
		if (ImGui::Button("Stop")) {
			playModeController_.RequestStop();
		}
		ImGui::EndDisabled();

		ImGui::Separator();
		switch (mode) {
		case aq::RunMode::Edit:  ImGui::TextUnformatted("Mode: Edit");  break;
		case aq::RunMode::Play:  ImGui::TextUnformatted("Mode: Play");  break;
		case aq::RunMode::Pause: ImGui::TextUnformatted("Mode: Pause"); break;
		}

		ImGui::Separator();
		if (playStopSelfTest_.IsRunning()) {
			ImGui::Text("Self Test: cycle %d/%d", std::min(playStopSelfTest_.GetDisplayCycle(), PlayStopSelfTest::CYCLE_COUNT),
			            PlayStopSelfTest::CYCLE_COUNT);
		} else {
			ImGui::BeginDisabled(isBusy || mode != aq::RunMode::Edit);
			if (ImGui::Button("Self Test")) {
				playStopSelfTest_.Start(false);
			}
			ImGui::EndDisabled();
		}
	}


	void EditorLayer::DrawInspector()
	{
		if (ImGui::Begin(INSPECTOR_WINDOW_NAME)) {
			// ImGui のフォントは ASCII のみを読み込んでいるので英語で出す
			const aq::level::LevelEditModel* model = editorContext_.GetModel();
			const aq::ecs::PrefabEditNode* node = nullptr;
			if (aq::AppHost::Get().GetRunMode() != aq::RunMode::Edit) {
				ImGui::TextUnformatted("Playing");
			} else {
				if (model != nullptr && editorContext_.GetSelectedId() != 0) {
					node = model->FindNode(editorContext_.GetSelectedId());
				}
				if (node != nullptr) {
					ImGui::Text("%s", node->name.c_str());
				} else {
					ImGui::TextUnformatted("No selection");
				}
			}
		}
		ImGui::End();
	}


	bool EditorLayer::SetupInitialLayout(const uint32_t dockspaceId)
	{
		if (isLayoutChecked_) return false;
		isLayoutChecked_ = true;

		// ヒエラルキーの配置が保存されていれば、利用者の配置を使う
		if (ImGui::FindWindowSettingsByID(ImHashStr(HierarchyPanel::WINDOW_NAME)) != nullptr) return false;

		// DockBuilder は ImGui の内部 API なので、使うのはこの関数だけにする
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::DockBuilderRemoveNode(dockspaceId);
		ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

		// 左を切り出し、残りから右を切り出す(右の割合は残りに対する割合に直す)
		ImGuiID leftId   = 0;
		ImGuiID restId   = 0;
		ImGuiID rightId  = 0;
		ImGuiID centerId = 0;
		ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Left, HIERARCHY_WIDTH_RATIO, &leftId, &restId);
		ImGui::DockBuilderSplitNode(restId, ImGuiDir_Right, INSPECTOR_WIDTH_RATIO / (1.0f - HIERARCHY_WIDTH_RATIO), &rightId, &centerId);

		ImGui::DockBuilderDockWindow(HierarchyPanel::WINDOW_NAME, leftId);
		ImGui::DockBuilderDockWindow(GAME_VIEW_WINDOW_NAME, centerId);
		ImGui::DockBuilderDockWindow(INSPECTOR_WINDOW_NAME, rightId);
		ImGui::DockBuilderFinish(dockspaceId);
		return true;
	}
}
