#include "aq.h"
#ifdef AQ_DEBUG_IMGUI
#include "PrefabEditNodeOps.h"
#include "ComponentRegistry.h"
#include <imgui/imgui.h>


namespace aq
{
	namespace ecs
	{
		util::JsonValue PrefabNodeToJson(const PrefabEditNode& node)
		{
			ComponentRegistry& registry = ComponentRegistry::Get();

			util::JsonValue obj = util::JsonValue::MakeObject();
			obj.Set("name", util::JsonValue(node.name));

			// 参照ノード: prefab パス + overrides（override/追加コンポーネント・削除リスト）。
			// components/children の実体は解決時に参照先から展開され、overrides が上書き適用される（§7.3）。
			if (!node.prefabRef.empty()) {
				obj.Set("prefab", util::JsonValue(node.prefabRef));

				util::JsonValue overrides = util::JsonValue::MakeObject();
				bool hasOverrides = false;

				// override/追加コンポーネント（deep-merge。base に無ければ追加される）
				bool hasComps = false;
				util::JsonValue comps = util::JsonValue::MakeObject();
				for (const PrefabEditComponent& c : node.components) {
					const ComponentMeta* meta = registry.Find(c.data.Type().GetHash());
					if (!meta || !meta->serializePtr || !meta->typeName) { continue; }
					util::JsonValue cj;
					meta->serializePtr(c.data.Get(), cj);
					comps.Set(meta->typeName, std::move(cj));
					hasComps = true;
				}
				if (hasComps) { overrides.Set("components", std::move(comps)); hasOverrides = true; }

				// 削除コンポーネント
				if (!node.removedComponents.empty()) {
					util::JsonValue removed = util::JsonValue::MakeArray();
					for (const std::string& t : node.removedComponents) { removed.PushBack(util::JsonValue(t)); }
					overrides.Set("removedComponents", std::move(removed));
					hasOverrides = true;
				}

				if (hasOverrides) { obj.Set("overrides", std::move(overrides)); }
				return obj;
			}

			util::JsonValue comps = util::JsonValue::MakeObject();
			for (const PrefabEditComponent& c : node.components) {
				const ComponentMeta* meta = registry.Find(c.data.Type().GetHash());
				if (!meta || !meta->serializePtr || !meta->typeName) { continue; }
				util::JsonValue cj;
				meta->serializePtr(c.data.Get(), cj);
				comps.Set(meta->typeName, std::move(cj));
			}
			obj.Set("components", std::move(comps));

			if (!node.children.empty()) {
				util::JsonValue arr = util::JsonValue::MakeArray();
				for (const auto& child : node.children) {
					arr.PushBack(PrefabNodeToJson(*child));
				}
				obj.Set("children", std::move(arr));
			}
			return obj;
		}


		std::unique_ptr<PrefabEditNode> PrefabNodeFromJson(const util::JsonValue& json)
		{
			auto node = std::make_unique<PrefabEditNode>();
			if (json.Contains("name")) { node->name = json["name"].AsString(); }

			ComponentRegistry& registry = ComponentRegistry::Get();

			// 参照ノード: prefab パス + overrides（override コンポーネント・削除リスト）を復元する。
			if (json.Contains("prefab") && json["prefab"].IsString()) {
				node->prefabRef = json["prefab"].AsString();

				if (json.Contains("overrides") && json["overrides"].IsObject()) {
					const util::JsonValue& ov = json["overrides"];
					if (ov.Contains("components") && ov["components"].IsObject()) {
						for (const auto& kv : ov["components"].GetObject()) {
							const TypeInfo t = registry.TypeOf(kv.first);
							if (t == TypeInfo()) { continue; }
							PrefabEditComponent comp{ AlignedStorage(t) };
							const ComponentMeta* meta = registry.Find(t.GetHash());
							if (meta && meta->deserializePtr) {
								meta->deserializePtr(comp.data.Get(), kv.second);
							}
							node->components.push_back(std::move(comp));
						}
					}
					if (ov.Contains("removedComponents") && ov["removedComponents"].IsArray()) {
						for (const util::JsonValue& r : ov["removedComponents"].GetArray()) {
							if (r.IsString()) { node->removedComponents.push_back(r.AsString()); }
						}
					}
				}
				return node;
			}

			if (json.Contains("components") && json["components"].IsObject()) {
				for (const auto& kv : json["components"].GetObject()) {
					const TypeInfo t = registry.TypeOf(kv.first);
					if (t == TypeInfo()) { continue; }                 // 未登録 typeName はスキップ
					PrefabEditComponent comp{ AlignedStorage(t) };
					const ComponentMeta* meta = registry.Find(t.GetHash());
					if (meta && meta->deserializePtr) {
						meta->deserializePtr(comp.data.Get(), kv.second);
					}
					node->components.push_back(std::move(comp));
				}
			}

			if (json.Contains("children") && json["children"].IsArray()) {
				for (const util::JsonValue& child : json["children"].GetArray()) {
					node->children.push_back(PrefabNodeFromJson(child));
				}
			}
			return node;
		}


		void PrefabNodeEnsureTransform(PrefabEditNode& node)
		{
			ComponentRegistry& registry = ComponentRegistry::Get();
			const TypeInfo transform = registry.TypeOf("Transform");
			if (transform == TypeInfo()) { return; }             // 未登録なら何もしない

			for (const PrefabEditComponent& c : node.components) {
				if (c.data.Type() == transform) { return; }      // 既に持っている
			}
			node.components.push_back(PrefabEditComponent{ AlignedStorage(transform) });
		}


		void PrefabNodeDrawTree(PrefabEditNode* node, PrefabEditNode*& selected,
			PrefabEditNode*& pendingDelete, const PrefabEditNode* root, const int depth,
			const bool ensureTransform)
		{
			constexpr int MAX_DEPTH = 64;
			if (!node || depth >= MAX_DEPTH) { return; }

			ImGui::PushID(node);

			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
			                         | ImGuiTreeNodeFlags_SpanAvailWidth
			                         | ImGuiTreeNodeFlags_DefaultOpen;
			if (selected == node)       { flags |= ImGuiTreeNodeFlags_Selected; }
			if (node->children.empty()) { flags |= ImGuiTreeNodeFlags_Leaf; }

			const bool opened = ImGui::TreeNodeEx("##node", flags, "%s", node->name.c_str());
			if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
				selected = node;
			}

			// 右クリック: 子追加 / 削除（削除は描画後に予約実行）
			if (ImGui::BeginPopupContextItem()) {
				if (ImGui::MenuItem("Add Child")) {
					auto child  = std::make_unique<PrefabEditNode>();
					child->name = "Child";
					if (ensureTransform) { PrefabNodeEnsureTransform(*child); }
					selected    = child.get();
					node->children.push_back(std::move(child));
				}
				if (node != root && ImGui::MenuItem("Delete")) {
					pendingDelete = node;
				}
				ImGui::EndPopup();
			}

			if (opened) {
				for (const auto& child : node->children) {
					PrefabNodeDrawTree(child.get(), selected, pendingDelete, root, depth + 1, ensureTransform);
				}
				ImGui::TreePop();
			}

			ImGui::PopID();
		}


		void PrefabNodeDrawInspector(PrefabEditNode& node, PrefabEditNode*& selected, const bool ensureTransform)
		{
			// 名前編集
			char nameBuf[128];
			std::snprintf(nameBuf, sizeof(nameBuf), "%s", node.name.c_str());
			if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) {
				node.name = nameBuf;
			}

			// Prefab 参照（非空 = 参照ノード。実体はロード時に参照先から解決・変更が反映される）
			{
				char refBuf[260];
				std::snprintf(refBuf, sizeof(refBuf), "%s", node.prefabRef.c_str());
				if (ImGui::InputText("Prefab Ref", refBuf, sizeof(refBuf))) {
					node.prefabRef = refBuf;
				}
			}

			ImGui::Separator();

			ComponentRegistry& registry = ComponentRegistry::Get();
			const bool isRef = !node.prefabRef.empty();

			// 参照ノードでは components を override として扱う（参照先へ deep-merge / 無ければ追加・§7.3）。
			if (isRef) {
				ImGui::TextWrapped("Overrides: components below are deep-merged onto the referenced prefab (added if absent).");
			}

			// コンポーネント実体のインスペクター（drawInspectorPtr で void* を編集）
			int removeIndex = -1;
			for (size_t i = 0; i < node.components.size(); ++i) {
				PrefabEditComponent& comp = node.components[i];
				const ComponentMeta* meta = registry.Find(comp.data.Type().GetHash());
				const char* label = (meta && meta->displayName) ? meta->displayName : "Component";

				ImGui::PushID(static_cast<int>(i));
				const bool open = ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen);
				if (ImGui::BeginPopupContextItem()) {
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
					if (ImGui::MenuItem("Remove Component")) { removeIndex = static_cast<int>(i); }
					ImGui::PopStyleColor();
					ImGui::EndPopup();
				}
				if (open && meta && meta->drawInspectorPtr) {
					meta->drawInspectorPtr(comp.data.Get());
				}
				ImGui::PopID();
			}
			if (removeIndex >= 0) {
				node.components.erase(node.components.begin() + removeIndex);
			}

			ImGui::Separator();

			// Add Component パレット（Reflect 化済み = drawInspectorPtr を持つ型のみ・重複は除外）
			if (ImGui::Button("+ Add Component")) {
				ImGui::OpenPopup("AddPrefabComponent");
			}
			if (ImGui::BeginPopup("AddPrefabComponent")) {
				for (const auto& [typeInfo, meta] : registry.GetAll()) {
					if (!meta.drawInspectorPtr || !meta.typeName) { continue; }

					bool present = false;
					for (const PrefabEditComponent& c : node.components) {
						if (c.data.Type() == typeInfo) { present = true; break; }
					}
					if (present) { continue; }

					if (ImGui::MenuItem(meta.displayName)) {
						node.components.push_back(PrefabEditComponent{ AlignedStorage(typeInfo) });
						ImGui::CloseCurrentPopup();
					}
				}
				ImGui::EndPopup();
			}

			// 参照ノード: 削除コンポーネント（override removedComponents）の編集。子追加はしない（子 override は未対応）。
			if (isRef) {
				ImGui::Separator();
				ImGui::TextDisabled("Removed Components (override)");

				int removeRmIndex = -1;
				for (size_t i = 0; i < node.removedComponents.size(); ++i) {
					ImGui::PushID(static_cast<int>(1000 + i));
					ImGui::BulletText("%s", node.removedComponents[i].c_str());
					ImGui::SameLine();
					if (ImGui::SmallButton("x")) { removeRmIndex = static_cast<int>(i); }
					ImGui::PopID();
				}
				if (removeRmIndex >= 0) { node.removedComponents.erase(node.removedComponents.begin() + removeRmIndex); }

				if (ImGui::Button("+ Remove Component")) { ImGui::OpenPopup("RemoveOverrideComponent"); }
				if (ImGui::BeginPopup("RemoveOverrideComponent")) {
					for (const auto& entry : registry.GetAll()) {
						const ComponentMeta& meta = entry.second;
						if (!meta.typeName) { continue; }
						bool present = false;
						for (const std::string& t : node.removedComponents) {
							if (t == meta.typeName) { present = true; break; }
						}
						if (present) { continue; }
						if (ImGui::MenuItem(meta.displayName)) {
							node.removedComponents.push_back(meta.typeName);
							ImGui::CloseCurrentPopup();
						}
					}
					ImGui::EndPopup();
				}
				return;
			}

			ImGui::SameLine();
			if (ImGui::Button("+ Add Child")) {
				auto child  = std::make_unique<PrefabEditNode>();
				child->name = "Child";
				if (ensureTransform) { PrefabNodeEnsureTransform(*child); }
				PrefabEditNode* added = child.get();
				node.children.push_back(std::move(child));
				selected = added;
			}
		}


		bool PrefabNodeRemove(PrefabEditNode* parent, PrefabEditNode* target)
		{
			if (!parent || !target) { return false; }
			for (size_t i = 0; i < parent->children.size(); ++i) {
				if (parent->children[i].get() == target) {
					parent->children.erase(parent->children.begin() + i);
					return true;
				}
				if (PrefabNodeRemove(parent->children[i].get(), target)) { return true; }
			}
			return false;
		}
	}
}
#endif
