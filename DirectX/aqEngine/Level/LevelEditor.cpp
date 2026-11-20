#include "aq.h"
#ifdef AQ_DEBUG_IMGUI
#include "Level/LevelEditor.h"
#include "Level/LevelManager.h"
#include "ECS/PrefabEditNodeOps.h"
#include <imgui/imgui.h>


namespace aq
{
	namespace level
	{
		LevelEditorPanel::LevelEditorPanel()
		{
			NewLevel();
		}


		void LevelEditorPanel::NewLevel()
		{
			// 最初の空エンティティ（Transform 必須）はモデルが用意する。それを選択する。
			model_.NewLevel();
			pendingDelete_ = nullptr;
			selected_      = model_.GetEntities().empty() ? nullptr : model_.GetEntities().front().get();
		}


		void LevelEditorPanel::DebugRenderMenu()
		{
			ImGui::MenuItem("Level Editor", nullptr, &show_);
		}


		void LevelEditorPanel::DebugRender()
		{
			// Load in World の要求を、前フレームまでの分もまとめて 1 回だけ処理する（ウィンドウを閉じていても行う）。
			// DebugRender は System の反復の外で呼ばれるので、GPU の完了待ちと Flush を行ってよい。
			model_.ProcessPreviewRequest();

			if (!show_) { return; }

			ImGui::SetNextWindowSize({ 780.0f, 560.0f }, ImGuiCond_FirstUseEver);
			if (!ImGui::Begin("Level Editor", &show_)) {
				ImGui::End();
				return;
			}

			// 再生中は中身を操作不可にする（ImGui のフォントは ASCII のみなので英語で出す）。
			// New / Load / Save / ノード編集 / Load in World / Load Async / Reload Refs はすべてウィンドウの中のウィジェット。
			if (locked_) {
				ImGui::TextUnformatted("Locked while playing");
			}
			ImGui::BeginDisabled(locked_);

			DrawToolbar();
			ImGui::Separator();

			// 左: entities ツリー + subLevels / 右: 選択ノードのインスペクター
			const float leftW = 280.0f;
			ImGui::BeginChild("##level_tree", ImVec2(leftW, 0.0f), true);
			ImGui::TextUnformatted("Entities");
			DrawEntityList(model_.GetEntities(), "top");
			ImGui::Separator();
			DrawSubLevels();
			ImGui::EndChild();

			// 削除予約の実行（描画後・ImGui スタックに影響しない安全なタイミング）。
			// トップレベル + 各インライン sub-level の entities を横断して外す。
			if (pendingDelete_) {
				bool handled = RemovePendingFrom(model_.GetEntities());
				if (!handled) {
					for (SubLevelEdit& sub : model_.GetSubLevels()) {
						if (sub.isInline && RemovePendingFrom(sub.entities)) { handled = true; break; }
					}
				}
				if (selected_ == pendingDelete_) { selected_ = nullptr; }
				pendingDelete_ = nullptr;
			}

			ImGui::SameLine();

			ImGui::BeginChild("##level_inspector", ImVec2(0.0f, 0.0f), true);
			DrawInspector();
			ImGui::EndChild();

			ImGui::EndDisabled();

			ImGui::End();
		}


		void LevelEditorPanel::DrawToolbar()
		{
			if (ImGui::Button("New")) { NewLevel(); }
			ImGui::SameLine();
			if (ImGui::Button("Load")) { Load(); }
			ImGui::SameLine();
			if (ImGui::Button("Save")) { Save(); }
			ImGui::SameLine();
			if (ImGui::Button("Load in World")) { model_.RequestPreview(); }   // 置き換えは次の DebugRender の先頭で行う
			ImGui::SameLine();
			if (ImGui::Button("Load Async")) {   // §15: フレーム分割ロード（保存済みファイルを非同期ロード）
				lastAsync_ = LevelManager::Get().LoadAsync(pathBuf_);
			}
			ImGui::SameLine();
			if (ImGui::Button("Reload Refs")) { LevelManager::Get().ReloadAll(); }   // D1: 手動リロード
			ImGui::SameLine();
			{
				bool autoReload = LevelManager::Get().IsAutoReload();
				if (ImGui::Checkbox("Auto", &autoReload)) { LevelManager::Get().SetAutoReload(autoReload); }  // D2: mtime 監視
			}

			{
				char nameBuf[128];
				std::snprintf(nameBuf, sizeof(nameBuf), "%s", model_.GetName().c_str());
				ImGui::SetNextItemWidth(200.0f);
				if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) { model_.SetName(nameBuf); }
			}
			ImGui::SetNextItemWidth(-1.0f);
			ImGui::InputText("##path", pathBuf_, sizeof(pathBuf_));

			// 非同期ロードの進捗（ローディング画面配線の検証表示）。
			if (lastAsync_.IsValid() && !lastAsync_.IsDone()) {
				char buf[64];
				std::snprintf(buf, sizeof(buf), "%u / %u", lastAsync_.Built(), lastAsync_.Total());
				ImGui::ProgressBar(lastAsync_.Progress(), ImVec2(-1.0f, 0.0f), buf);
			}
		}


		void LevelEditorPanel::DrawEntityList(std::vector<std::unique_ptr<ecs::PrefabEditNode>>& list, const char* idLabel)
		{
			ImGui::PushID(idLabel);
			if (ImGui::SmallButton("+ Entity")) { AddEntityNode(list, false); }
			ImGui::SameLine();
			if (ImGui::SmallButton("+ Prefab")) { AddEntityNode(list, true); }

			// 各ノードは削除可能にするため root=nullptr、Transform 必須で描画する。
			for (const auto& entity : list) {
				ecs::PrefabNodeDrawTree(entity.get(), selected_, pendingDelete_, nullptr, 0, true);
			}
			ImGui::PopID();
		}


		void LevelEditorPanel::AddEntityNode(std::vector<std::unique_ptr<ecs::PrefabEditNode>>& list, const bool asPrefab)
		{
			auto entity = std::make_unique<ecs::PrefabEditNode>();
			if (asPrefab) {
				entity->name      = "Prefab";
				entity->prefabRef = "Assets/Prefabs/New.prefab.json";   // 参照ノード（パスは Inspector で編集）
			} else {
				entity->name = "Entity";
				ecs::PrefabNodeEnsureTransform(*entity);                 // 実体ノードは Transform 必須
			}
			selected_ = entity.get();
			list.push_back(std::move(entity));
		}


		bool LevelEditorPanel::RemovePendingFrom(std::vector<std::unique_ptr<ecs::PrefabEditNode>>& list)
		{
			for (size_t i = 0; i < list.size(); ++i) {
				if (list[i].get() == pendingDelete_) {
					list.erase(list.begin() + i);
					return true;
				}
				if (ecs::PrefabNodeRemove(list[i].get(), pendingDelete_)) {
					return true;
				}
			}
			return false;
		}


		void LevelEditorPanel::DrawInspector()
		{
			if (!selected_) {
				ImGui::TextDisabled("No node selected.");
				return;
			}
			ecs::PrefabNodeDrawInspector(*selected_, selected_, true);
		}


		void LevelEditorPanel::DrawSubLevels()
		{
			// UI は 1 段目だけを編集する。インラインの中の入れ子の subLevels は表示しないが、モデルが保存で保持する。
			std::vector<SubLevelEdit>& subLevels = model_.GetSubLevels();

			ImGui::TextUnformatted("Sub Levels");
			ImGui::SameLine();
			if (ImGui::SmallButton("+ File")) {   // 外部 .level.json 参照
				SubLevelEdit e; e.isInline = false; subLevels.push_back(std::move(e));
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("+ Inline")) {  // その場で作るインライン定義
				SubLevelEdit e; e.isInline = true; subLevels.push_back(std::move(e));
			}

			int removeIndex = -1;
			for (size_t i = 0; i < subLevels.size(); ++i) {
				SubLevelEdit& sub = subLevels[i];
				ImGui::PushID(static_cast<int>(i));

				if (sub.isInline) {
					char nameBuf[128];
					std::snprintf(nameBuf, sizeof(nameBuf), "%s", sub.name.c_str());
					ImGui::SetNextItemWidth(150.0f);
					if (ImGui::InputText("##name", nameBuf, sizeof(nameBuf))) { sub.name = nameBuf; }
					ImGui::SameLine();
					ImGui::Checkbox("start", &sub.loadOnStart);
					ImGui::SameLine();
					if (ImGui::SmallButton("x")) { removeIndex = static_cast<int>(i); }

					// インライン sub-level の entities をその場で編集する。
					ImGui::Indent();
					DrawEntityList(sub.entities, "inline");
					ImGui::Unindent();
				} else {
					char pathBuf[260];
					std::snprintf(pathBuf, sizeof(pathBuf), "%s", sub.path.c_str());
					ImGui::SetNextItemWidth(150.0f);
					if (ImGui::InputText("##sub", pathBuf, sizeof(pathBuf))) { sub.path = pathBuf; }
					ImGui::SameLine();
					ImGui::Checkbox("start", &sub.loadOnStart);
					ImGui::SameLine();
					if (ImGui::SmallButton("x")) { removeIndex = static_cast<int>(i); }
				}
				ImGui::PopID();
			}
			if (removeIndex >= 0) { subLevels.erase(subLevels.begin() + removeIndex); }
		}


		void LevelEditorPanel::Save()
		{
			if (locked_) { return; }
			// 一時ファイルに書いてから置き換える（エディタ全体設計 7.7 節）。
			if (!model_.SaveFile(pathBuf_)) {
				EngineAssertMsg(false, "LevelEditor::Save: failed to write level JSON file");
			}
		}


		void LevelEditorPanel::Load()
		{
			if (locked_) { return; }
			// 失敗したときモデルは変わらないので、選択もそのままにする。
			if (!model_.LoadFile(pathBuf_)) {
				EngineAssertMsg(false, "LevelEditor::Load: failed to parse level JSON file");
				return;
			}

			pendingDelete_ = nullptr;
			selected_      = model_.GetEntities().empty() ? nullptr : model_.GetEntities().front().get();
		}
	}
}
#endif
