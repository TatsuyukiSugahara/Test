#include "aq.h"
#ifdef AQ_DEBUG_IMGUI
#include "Level/LevelEditor.h"
#include "Level/LevelManager.h"
#include "Level/LevelRegistry.h"
#include "Level/LevelSerializer.h"
#include "ECS/PrefabEditNodeOps.h"
#include <imgui/imgui.h>
#include <string>


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
			name_ = "Level";
			entities_.clear();
			subLevels_.clear();
			pendingDelete_ = nullptr;

			// 最初の空エンティティを 1 つ用意する（Transform 必須）。
			auto entity  = std::make_unique<ecs::PrefabEditNode>();
			entity->name = "Entity";
			ecs::PrefabNodeEnsureTransform(*entity);
			selected_    = entity.get();
			entities_.push_back(std::move(entity));
		}


		void LevelEditorPanel::DebugRenderMenu()
		{
			ImGui::MenuItem("Level Editor", nullptr, &show_);
		}


		void LevelEditorPanel::DebugRender()
		{
			if (!show_) { return; }

			ImGui::SetNextWindowSize({ 780.0f, 560.0f }, ImGuiCond_FirstUseEver);
			if (!ImGui::Begin("Level Editor", &show_)) {
				ImGui::End();
				return;
			}

			DrawToolbar();
			ImGui::Separator();

			// 左: entities ツリー + subLevels / 右: 選択ノードのインスペクター
			const float leftW = 280.0f;
			ImGui::BeginChild("##level_tree", ImVec2(leftW, 0.0f), true);
			ImGui::TextUnformatted("Entities");
			DrawEntityList(entities_, "top");
			ImGui::Separator();
			DrawSubLevels();
			ImGui::EndChild();

			// 削除予約の実行（描画後・ImGui スタックに影響しない安全なタイミング）。
			// トップレベル + 各インライン sub-level の entities を横断して外す。
			if (pendingDelete_) {
				bool handled = RemovePendingFrom(entities_);
				if (!handled) {
					for (SubLevelEdit& sub : subLevels_) {
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
			if (ImGui::Button("Load in World")) { LoadInWorld(); }
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
				std::snprintf(nameBuf, sizeof(nameBuf), "%s", name_.c_str());
				ImGui::SetNextItemWidth(200.0f);
				if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) { name_ = nameBuf; }
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
			ImGui::TextUnformatted("Sub Levels");
			ImGui::SameLine();
			if (ImGui::SmallButton("+ File")) {   // 外部 .level.json 参照
				SubLevelEdit e; e.isInline = false; subLevels_.push_back(std::move(e));
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("+ Inline")) {  // その場で作るインライン定義
				SubLevelEdit e; e.isInline = true; subLevels_.push_back(std::move(e));
			}

			int removeIndex = -1;
			for (size_t i = 0; i < subLevels_.size(); ++i) {
				SubLevelEdit& sub = subLevels_[i];
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
			if (removeIndex >= 0) { subLevels_.erase(subLevels_.begin() + removeIndex); }
		}


		util::JsonValue LevelEditorPanel::BuildJson() const
		{
			util::JsonValue root = util::JsonValue::MakeObject();
			root.Set("name", util::JsonValue(name_));

			util::JsonValue entities = util::JsonValue::MakeArray();
			for (const auto& entity : entities_) {
				entities.PushBack(ecs::PrefabNodeToJson(*entity));
			}
			root.Set("entities", std::move(entities));

			if (!subLevels_.empty()) {
				util::JsonValue subs = util::JsonValue::MakeArray();
				for (const SubLevelEdit& sub : subLevels_) {
					util::JsonValue obj = util::JsonValue::MakeObject();
					if (sub.isInline) {
						// インライン定義: level を { name, entities } オブジェクトとして埋め込む。
						util::JsonValue lvl = util::JsonValue::MakeObject();
						lvl.Set("name", util::JsonValue(sub.name));
						util::JsonValue ents = util::JsonValue::MakeArray();
						for (const auto& entity : sub.entities) {
							ents.PushBack(ecs::PrefabNodeToJson(*entity));
						}
						lvl.Set("entities", std::move(ents));
						obj.Set("level", std::move(lvl));
					} else {
						// 外部ファイル参照: level を文字列パスにする。
						obj.Set("level", util::JsonValue(sub.path));
					}
					obj.Set("loadOnStart", util::JsonValue(sub.loadOnStart));
					subs.PushBack(std::move(obj));
				}
				root.Set("subLevels", std::move(subs));
			}
			return root;
		}


		void LevelEditorPanel::Save()
		{
			const util::JsonValue root = BuildJson();
			if (!util::JsonSerializer::WriteFile(pathBuf_, root)) {
				EngineAssertMsg(false, "LevelEditor::Save: failed to write level JSON file");
			}
		}


		void LevelEditorPanel::Load()
		{
			const util::JsonValue root = util::JsonParser::ParseFile(pathBuf_);
			if (root.IsNull()) {
				EngineAssertMsg(false, "LevelEditor::Load: failed to parse level JSON file");
				return;
			}

			name_ = root.Contains("name") ? root["name"].AsString() : std::string("Level");
			entities_.clear();
			subLevels_.clear();
			selected_ = nullptr;

			// entity ノードは PrefabNodeFromJson が "prefab" を prefabRef として復元する（参照ノードもそのまま往復）。
			if (root.Contains("entities") && root["entities"].IsArray()) {
				for (const util::JsonValue& node : root["entities"].GetArray()) {
					entities_.push_back(ecs::PrefabNodeFromJson(node));
				}
			}

			if (root.Contains("subLevels") && root["subLevels"].IsArray()) {
				for (const util::JsonValue& sub : root["subLevels"].GetArray()) {
					if (!sub.IsObject() || !sub.Contains("level")) { continue; }
					SubLevelEdit edit;
					edit.loadOnStart = sub.Contains("loadOnStart") ? sub["loadOnStart"].AsBool(true) : true;

					const util::JsonValue& lv = sub["level"];
					if (lv.IsObject()) {
						// インライン定義: name + entities を復元する。
						edit.isInline = true;
						edit.name = lv.Contains("name") ? lv["name"].AsString() : std::string("SubLevel");
						if (lv.Contains("entities") && lv["entities"].IsArray()) {
							for (const util::JsonValue& e : lv["entities"].GetArray()) {
								edit.entities.push_back(ecs::PrefabNodeFromJson(e));
							}
						}
					} else if (lv.IsString()) {
						// 外部ファイル参照。
						edit.isInline = false;
						edit.path = lv.AsString();
					} else {
						continue;
					}
					subLevels_.push_back(std::move(edit));
				}
			}

			if (!entities_.empty()) { selected_ = entities_.front().get(); }
		}


		void LevelEditorPanel::LoadInWorld()
		{
			// 現在の編集内容を JSON 化 → in-memory 登録 → LevelManager でワールドへ遅延ロードする。
			// 毎回作り直すためユニークキーで登録し直す（キャッシュのスタール回避）。
			const util::JsonValue root = BuildJson();
			std::shared_ptr<const LevelData> data = LevelSerializer::FromJson(root);
			if (!data) { return; }

			static int previewCounter = 0;
			const std::string key = "<level-editor-preview-" + std::to_string(previewCounter++) + ">";
			LevelRegistry::Get().Register(key, data);
			LevelManager::Get().Load(key);
		}
	}
}
#endif
