#include "aq.h"
#ifdef AQ_DEBUG_IMGUI
#include "PrefabEditor.h"
#include "ComponentRegistry.h"
#include "PrefabSerializer.h"
#include "Prefab.h"
#include "EntityContext.h"
#include <imgui/imgui.h>

namespace aq
{
	namespace ecs
	{
		PrefabEditorPanel::PrefabEditorPanel()
		{
			NewPrefab();
		}


		void PrefabEditorPanel::NewPrefab()
		{
			root_ = std::make_unique<PrefabEditNode>();
			root_->name = "Root";
			selected_   = root_.get();
		}


		// ── メニュー / エントリ ───────────────────────────────────────────────────

		void PrefabEditorPanel::DebugRenderMenu()
		{
			ImGui::MenuItem("Prefab Editor", nullptr, &show_);
		}


		void PrefabEditorPanel::DebugRender()
		{
			if (!show_) return;

			ImGui::SetNextWindowSize({ 720.0f, 520.0f }, ImGuiCond_FirstUseEver);
			if (!ImGui::Begin("Prefab Editor", &show_))
			{
				ImGui::End();
				return;
			}

			DrawToolbar();
			ImGui::Separator();

			// 左: 階層ツリー / 右: 選択ノードのインスペクター
			const float leftW = 240.0f;
			ImGui::BeginChild("##prefab_tree", ImVec2(leftW, 0.0f), true);
			if (root_) DrawTree(root_.get(), 0);
			ImGui::EndChild();

			// 描画後に削除予約ノードを除去する（ImGui スタックに影響しない安全なタイミング）。
			if (pendingDelete_)
			{
				if (selected_ == pendingDelete_) selected_ = root_.get();
				RemoveNode(root_.get(), pendingDelete_);
				pendingDelete_ = nullptr;
			}

			ImGui::SameLine();

			ImGui::BeginChild("##prefab_inspector", ImVec2(0.0f, 0.0f), true);
			DrawInspector();
			ImGui::EndChild();

			ImGui::End();
		}


		// ── ツールバー ───────────────────────────────────────────────────────────

		void PrefabEditorPanel::DrawToolbar()
		{
			if (ImGui::Button("New")) NewPrefab();
			ImGui::SameLine();
			if (ImGui::Button("Load")) Load();
			ImGui::SameLine();
			if (ImGui::Button("Save")) Save();
			ImGui::SameLine();
			if (ImGui::Button("Spawn Preview")) SpawnPreview();

			ImGui::SetNextItemWidth(-1.0f);
			ImGui::InputText("##path", pathBuf_, sizeof(pathBuf_));
		}


		// ── 階層ツリー ───────────────────────────────────────────────────────────

		void PrefabEditorPanel::DrawTree(PrefabEditNode* node, int depth)
		{
			constexpr int kMaxDepth = 64;
			if (!node || depth >= kMaxDepth) return;

			ImGui::PushID(node);

			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
			                         | ImGuiTreeNodeFlags_SpanAvailWidth
			                         | ImGuiTreeNodeFlags_DefaultOpen;
			if (selected_ == node)        flags |= ImGuiTreeNodeFlags_Selected;
			if (node->children.empty())   flags |= ImGuiTreeNodeFlags_Leaf;

			const bool opened = ImGui::TreeNodeEx("##node", flags, "%s", node->name.c_str());
			if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
				selected_ = node;

			// 右クリック: 子追加 / 削除（削除は描画後に予約実行）
			if (ImGui::BeginPopupContextItem())
			{
				if (ImGui::MenuItem("Add Child"))
				{
					auto child  = std::make_unique<PrefabEditNode>();
					child->name = "Child";
					selected_   = child.get();
					node->children.push_back(std::move(child));
				}
				if (node != root_.get() && ImGui::MenuItem("Delete"))
					pendingDelete_ = node;
				ImGui::EndPopup();
			}

			if (opened)
			{
				for (const auto& child : node->children)
					DrawTree(child.get(), depth + 1);
				ImGui::TreePop();
			}

			ImGui::PopID();
		}


		bool PrefabEditorPanel::RemoveNode(PrefabEditNode* parent, PrefabEditNode* target)
		{
			if (!parent || !target) return false;
			for (size_t i = 0; i < parent->children.size(); ++i)
			{
				if (parent->children[i].get() == target)
				{
					parent->children.erase(parent->children.begin() + i);
					return true;
				}
				if (RemoveNode(parent->children[i].get(), target)) return true;
			}
			return false;
		}


		// ── インスペクター ───────────────────────────────────────────────────────

		void PrefabEditorPanel::DrawInspector()
		{
			if (!selected_)
			{
				ImGui::TextDisabled("No node selected.");
				return;
			}

			// 名前編集
			char nameBuf[128];
			std::snprintf(nameBuf, sizeof(nameBuf), "%s", selected_->name.c_str());
			if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
				selected_->name = nameBuf;

			ImGui::Separator();

			ComponentRegistry& registry = ComponentRegistry::Get();

			// コンポーネント実体のインスペクター（drawInspectorPtr で void* を編集）
			int removeIndex = -1;
			for (size_t i = 0; i < selected_->components.size(); ++i)
			{
				PrefabEditComponent& comp = selected_->components[i];
				const ComponentMeta* meta = registry.Find(comp.data.Type().GetHash());
				const char* label = (meta && meta->displayName) ? meta->displayName : "Component";

				ImGui::PushID(static_cast<int>(i));
				const bool open = ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen);
				if (ImGui::BeginPopupContextItem())
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
					if (ImGui::MenuItem("Remove Component")) removeIndex = static_cast<int>(i);
					ImGui::PopStyleColor();
					ImGui::EndPopup();
				}
				if (open && meta && meta->drawInspectorPtr)
					meta->drawInspectorPtr(comp.data.Get());
				ImGui::PopID();
			}
			if (removeIndex >= 0)
				selected_->components.erase(selected_->components.begin() + removeIndex);

			ImGui::Separator();

			// Add Component パレット（Reflect 化済み = drawInspectorPtr を持つ型のみ・重複は除外）
			if (ImGui::Button("+ Add Component"))
				ImGui::OpenPopup("AddPrefabComponent");
			if (ImGui::BeginPopup("AddPrefabComponent"))
			{
				for (const auto& [typeInfo, meta] : registry.GetAll())
				{
					if (!meta.drawInspectorPtr || !meta.typeName) continue;

					bool present = false;
					for (const PrefabEditComponent& c : selected_->components)
						if (c.data.Type() == typeInfo) { present = true; break; }
					if (present) continue;

					if (ImGui::MenuItem(meta.displayName))
					{
						selected_->components.push_back(PrefabEditComponent{ AlignedStorage(typeInfo) });
						ImGui::CloseCurrentPopup();
					}
				}
				ImGui::EndPopup();
			}

			ImGui::SameLine();
			if (ImGui::Button("+ Add Child"))
			{
				auto child  = std::make_unique<PrefabEditNode>();
				child->name = "Child";
				PrefabEditNode* added = child.get();
				selected_->children.push_back(std::move(child));
				selected_ = added;
			}
		}


		// ── 編集ツリー ⇄ JSON ────────────────────────────────────────────────────

		util::JsonValue PrefabEditorPanel::NodeToJson(const PrefabEditNode& node) const
		{
			ComponentRegistry& registry = ComponentRegistry::Get();

			util::JsonValue obj = util::JsonValue::MakeObject();
			obj.Set("name", util::JsonValue(node.name));

			util::JsonValue comps = util::JsonValue::MakeObject();
			for (const PrefabEditComponent& c : node.components)
			{
				const ComponentMeta* meta = registry.Find(c.data.Type().GetHash());
				if (!meta || !meta->serializePtr || !meta->typeName) continue;
				util::JsonValue cj;
				meta->serializePtr(c.data.Get(), cj);
				comps.Set(meta->typeName, std::move(cj));
			}
			obj.Set("components", std::move(comps));

			if (!node.children.empty())
			{
				util::JsonValue arr = util::JsonValue::MakeArray();
				for (const auto& child : node.children)
					arr.PushBack(NodeToJson(*child));
				obj.Set("children", std::move(arr));
			}
			return obj;
		}


		std::unique_ptr<PrefabEditNode> PrefabEditorPanel::JsonToNode(const util::JsonValue& json) const
		{
			ComponentRegistry& registry = ComponentRegistry::Get();

			auto node = std::make_unique<PrefabEditNode>();
			if (json.Contains("name")) node->name = json["name"].AsString();

			if (json.Contains("components") && json["components"].IsObject())
			{
				for (const auto& kv : json["components"].GetObject())
				{
					const TypeInfo t = registry.TypeOf(kv.first);
					if (t == TypeInfo()) continue;                 // 未登録 typeName はスキップ
					PrefabEditComponent comp{ AlignedStorage(t) };
					const ComponentMeta* meta = registry.Find(t.GetHash());
					if (meta && meta->deserializePtr)
						meta->deserializePtr(comp.data.Get(), kv.second);
					node->components.push_back(std::move(comp));
				}
			}

			if (json.Contains("children") && json["children"].IsArray())
				for (const util::JsonValue& child : json["children"].GetArray())
					node->children.push_back(JsonToNode(child));

			return node;
		}


		// ── Save / Load / Spawn ───────────────────────────────────────────────────

		void PrefabEditorPanel::Save()
		{
			if (!root_) return;
			const util::JsonValue json = NodeToJson(*root_);
			if (!util::JsonSerializer::WriteFile(pathBuf_, json))
				EngineAssertMsg(false, "PrefabEditor::Save: failed to write prefab JSON file");
		}


		void PrefabEditorPanel::Load()
		{
			const util::JsonValue json = util::JsonParser::ParseFile(pathBuf_);
			if (json.IsNull())
			{
				EngineAssertMsg(false, "PrefabEditor::Load: failed to parse prefab JSON file");
				return;
			}
			root_     = JsonToNode(json);
			selected_ = root_.get();
		}


		void PrefabEditorPanel::SpawnPreview()
		{
			if (!root_) return;
			// 編集ツリー → JSON → ランタイム Prefab（FromJson）→ 遅延生成。Reflect 経路を共有する。
			const util::JsonValue json = NodeToJson(*root_);
			Prefab prefab = PrefabSerializer::FromJson(json);
			if (prefab.IsValid())
				prefab.Instantiate();
		}
	}
}
#endif
