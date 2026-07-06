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
			DrawEntities();
			ImGui::Separator();
			DrawSubLevels();
			ImGui::EndChild();

			// 削除予約の実行（描画後・ImGui スタックに影響しない安全なタイミング）。
			// トップレベル entity が対象なら entities_ から、そうでなければ木から外す。
			if (pendingDelete_) {
				for (size_t i = 0; i < entities_.size(); ++i) {
					if (entities_[i].get() == pendingDelete_) {
						entities_.erase(entities_.begin() + i);
						break;
					}
					if (ecs::PrefabNodeRemove(entities_[i].get(), pendingDelete_)) {
						break;
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

			{
				char nameBuf[128];
				std::snprintf(nameBuf, sizeof(nameBuf), "%s", name_.c_str());
				ImGui::SetNextItemWidth(200.0f);
				if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) { name_ = nameBuf; }
			}
			ImGui::SetNextItemWidth(-1.0f);
			ImGui::InputText("##path", pathBuf_, sizeof(pathBuf_));
		}


		void LevelEditorPanel::DrawEntities()
		{
			ImGui::TextUnformatted("Entities");
			ImGui::SameLine();
			if (ImGui::SmallButton("+ Entity")) {
				auto entity  = std::make_unique<ecs::PrefabEditNode>();
				entity->name = "Entity";
				ecs::PrefabNodeEnsureTransform(*entity);
				selected_    = entity.get();
				entities_.push_back(std::move(entity));
			}

			// 各トップレベル entity は削除可能にするため root=nullptr、Transform 必須で描画する。
			for (const auto& entity : entities_) {
				ecs::PrefabNodeDrawTree(entity.get(), selected_, pendingDelete_, nullptr, 0, true);
			}
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
			if (ImGui::SmallButton("+ Sub")) {
				subLevels_.push_back(SubLevelEdit{});
			}

			int removeIndex = -1;
			for (size_t i = 0; i < subLevels_.size(); ++i) {
				ImGui::PushID(static_cast<int>(i));
				char pathBuf[260];
				std::snprintf(pathBuf, sizeof(pathBuf), "%s", subLevels_[i].path.c_str());
				ImGui::SetNextItemWidth(150.0f);
				if (ImGui::InputText("##sub", pathBuf, sizeof(pathBuf))) { subLevels_[i].path = pathBuf; }
				ImGui::SameLine();
				ImGui::Checkbox("start", &subLevels_[i].loadOnStart);
				ImGui::SameLine();
				if (ImGui::SmallButton("x")) { removeIndex = static_cast<int>(i); }
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
					obj.Set("level", util::JsonValue(sub.path));
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

			// ※ entity ノードの "prefab" 参照展開はエディタでは行わない（インライン components のみ復元）。
			//    参照モードの編集は L7d で対応する。
			if (root.Contains("entities") && root["entities"].IsArray()) {
				for (const util::JsonValue& node : root["entities"].GetArray()) {
					entities_.push_back(ecs::PrefabNodeFromJson(node));
				}
			}

			if (root.Contains("subLevels") && root["subLevels"].IsArray()) {
				for (const util::JsonValue& sub : root["subLevels"].GetArray()) {
					if (!sub.IsObject() || !sub.Contains("level")) { continue; }
					SubLevelEdit edit;
					edit.path        = sub["level"].AsString();
					edit.loadOnStart = sub.Contains("loadOnStart") ? sub["loadOnStart"].AsBool(true) : true;
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
