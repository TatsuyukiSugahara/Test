#include "aq.h"
#ifdef AQ_DEBUG_IMGUI
#include "PrefabEditNodeOps.h"
#include "ComponentRegistry.h"
#include <imgui/imgui.h>


namespace aq
{
	namespace ecs
	{
		namespace
		{
			/** overrides の中のキー */
			static constexpr const char* KEY_NAME               = "name";
			static constexpr const char* KEY_COMPONENTS         = "components";
			static constexpr const char* KEY_ADDED_COMPONENTS   = "addedComponents";
			static constexpr const char* KEY_REMOVED_COMPONENTS = "removedComponents";


			/** 2 つの JSON が等しいか（キーの順序は無視、数値は double で完全一致） */
			bool JsonEquals(const util::JsonValue& a, const util::JsonValue& b)
			{
				if (a.IsNumber() && b.IsNumber()) { return a.AsDouble() == b.AsDouble(); }
				if (a.IsNull() && b.IsNull())     { return true; }
				if (a.IsBool() && b.IsBool())     { return a.AsBool() == b.AsBool(); }
				if (a.IsString() && b.IsString()) { return a.AsString() == b.AsString(); }
				if (a.IsArray() && b.IsArray()) {
					if (a.Size() != b.Size()) { return false; }
					for (size_t i = 0; i < a.Size(); ++i) {
						if (!JsonEquals(a[i], b[i])) { return false; }
					}
					return true;
				}
				if (a.IsObject() && b.IsObject()) {
					if (a.GetObject().size() != b.GetObject().size()) { return false; }
					for (const auto& [key, value] : a.GetObject()) {
						if (!b.Contains(key) || !JsonEquals(value, b[key])) { return false; }
					}
					return true;
				}
				return false;
			}


			/**
			 * before から after への「違うキー」を求める（JsonValue::Merge の規則と同じ）
			 *
			 * オブジェクトは中まで比べて違う葉だけを残し、配列とスカラーは値ごと置き換える。
			 * @return 違いがあれば true（out に差分を入れる）
			 */
			bool MakeJsonDiff(const util::JsonValue& before, const util::JsonValue& after, util::JsonValue& out)
			{
				if (before.IsObject() && after.IsObject()) {
					util::JsonValue diff = util::JsonValue::MakeObject();
					bool            changed = false;
					for (const auto& [key, value] : after.GetObject()) {
						if (!before.Contains(key)) {
							diff.Set(key, value);
							changed = true;
							continue;
						}
						util::JsonValue child;
						if (MakeJsonDiff(before[key], value, child)) {
							diff.Set(key, std::move(child));
							changed = true;
						}
					}
					if (changed) { out = std::move(diff); }
					return changed;
				}
				if (JsonEquals(before, after)) { return false; }
				out = after;
				return true;
			}


			/** オブジェクトから key を除く（JsonValue に削除が無いので作り直す） */
			void EraseKey(util::JsonValue& object, const std::string& key)
			{
				if (!object.Contains(key)) { return; }
				util::JsonValue rebuilt = util::JsonValue::MakeObject();
				for (const auto& [name, value] : object.GetObject()) {
					if (name != key) { rebuilt.Set(name, value); }
				}
				object = std::move(rebuilt);
			}


			/** overrides.<section> から typeName を除く。除いて空になった section はキーごと消す */
			void EraseSectionMember(util::JsonValue& overrides, const char* section, const std::string& typeName)
			{
				if (!overrides[section].Contains(typeName)) { return; }
				util::JsonValue inner = overrides[section];
				EraseKey(inner, typeName);
				if (inner.GetObject().empty()) {
					EraseKey(overrides, section);
				} else {
					overrides.Set(section, std::move(inner));
				}
			}


			/** overrides.<section>.<typeName> を value で置き換える（section が無ければ作る） */
			void SetSectionMember(util::JsonValue& overrides, const char* section, const std::string& typeName, const util::JsonValue& value)
			{
				util::JsonValue inner = overrides[section].IsObject() ? overrides[section] : util::JsonValue::MakeObject();
				inner.Set(typeName, value);
				overrides.Set(section, std::move(inner));
			}


			/** overrides.<section>.<typeName> へ diff を deep-merge する（既存の override の意味どおり） */
			void MergeSectionMember(util::JsonValue& overrides, const char* section, const std::string& typeName, const util::JsonValue& diff)
			{
				util::JsonValue inner = overrides[section].IsObject() ? overrides[section] : util::JsonValue::MakeObject();
				if (inner.Contains(typeName)) {
					util::JsonValue merged = inner[typeName];
					merged.Merge(diff);
					inner.Set(typeName, std::move(merged));
				} else {
					inner.Set(typeName, diff);
				}
				overrides.Set(section, std::move(inner));
			}


			bool HasComponent(const PrefabEditNode& node, const TypeInfo& type)
			{
				for (const PrefabEditComponent& c : node.components) {
					if (c.data.Type() == type) { return true; }
				}
				return false;
			}


			/**
			 * 読み込んだときにあったのに今の一覧に無い override コンポーネントを、overrides.<section> から消す
			 *
			 * 読み込みは登録済みの型だけを編集ノードへ入れるので、登録済みで一覧に無い型を「消えた」とみなす
			 * （未登録の型は読み込んでいないので、生 JSON のまま残す）。
			 */
			void EraseMissingComponents(util::JsonValue& overrides, const char* section, const PrefabEditNode& node)
			{
				ComponentRegistry& registry = ComponentRegistry::Get();
				std::vector<std::string> missing;
				for (const auto& [typeName, value] : overrides[section].GetObject()) {
					const TypeInfo t = registry.TypeOf(typeName);
					if (t == TypeInfo()) { continue; }
					if (!HasComponent(node, t)) { missing.push_back(typeName); }
				}
				for (const std::string& typeName : missing) {
					EraseSectionMember(overrides, section, typeName);
				}
			}


			/**
			 * 参照ノードの overrides を作る（エディタ全体設計 7.8 節「2. override の保持」）
			 *
			 * 読み込んだ生 JSON（overridesRaw）を写して始め、編集で変わったところだけを書き換える。
			 * children / removedChildren は触らない（編集 UI が無い）。
			 */
			util::JsonValue BuildReferenceOverrides(const PrefabEditNode& node)
			{
				ComponentRegistry& registry = ComponentRegistry::Get();

				util::JsonValue overrides = node.overridesRaw.IsObject() ? node.overridesRaw : util::JsonValue::MakeObject();

				// 名前: overrides.name があったときは、Name 欄の編集が実行時の名前に効くようにそちらも書く
				if (overrides.Contains(KEY_NAME)) {
					overrides.Set(KEY_NAME, util::JsonValue(node.name));
				}

				// 読み込んだのに今の一覧に無い override コンポーネントは、読み込んだ出どころから消す（両方にあれば両方から）
				EraseMissingComponents(overrides, KEY_COMPONENTS, node);
				EraseMissingComponents(overrides, KEY_ADDED_COMPONENTS, node);

				for (const PrefabEditComponent& c : node.components) {
					const ComponentMeta* meta = registry.Find(c.data.Type().GetHash());
					if (!meta || !meta->serializePtr || !meta->typeName) { continue; }
					const std::string typeName = meta->typeName;

					util::JsonValue current;
					meta->serializePtr(c.data.Get(), current);

					if (c.loadedJson.IsNull()) {
						// 新しく足した: components に全フィールドを書く。addedComponents は後から適用されるので、
						// 同じ型が残っていると新しい値が上書きされる。そこから消す
						SetSectionMember(overrides, KEY_COMPONENTS, typeName, current);
						EraseSectionMember(overrides, KEY_ADDED_COMPONENTS, typeName);
					} else if (c.source == PrefabOverrideSource::AddedComponents) {
						// addedComponents は丸ごと上書きする意味なので、変わっていれば全フィールドを書き戻す
						if (!JsonEquals(c.loadedJson, current)) {
							SetSectionMember(overrides, KEY_ADDED_COMPONENTS, typeName, current);
						}
					} else {
						// components は deep-merge なので、違うキーだけを書き足す（元に戻したキーは書かない）
						util::JsonValue diff;
						if (MakeJsonDiff(c.loadedJson, current, diff)) {
							MergeSectionMember(overrides, KEY_COMPONENTS, typeName, diff);
						}
					}
				}

				// 削除コンポーネント: 今の一覧で置き換える（空ならキーを消す）。変わっていなければ生 JSON のまま
				{
					std::vector<std::string> loaded;
					for (const util::JsonValue& r : overrides[KEY_REMOVED_COMPONENTS].GetArray()) {
						if (r.IsString()) { loaded.push_back(r.AsString()); }
					}
					if (loaded != node.removedComponents) {
						if (node.removedComponents.empty()) {
							EraseKey(overrides, KEY_REMOVED_COMPONENTS);
						} else {
							util::JsonValue removed = util::JsonValue::MakeArray();
							for (const std::string& t : node.removedComponents) { removed.PushBack(util::JsonValue(t)); }
							overrides.Set(KEY_REMOVED_COMPONENTS, std::move(removed));
						}
					}
				}
				return overrides;
			}


			/**
			 * 参照ノードの override コンポーネントを 1 つ読み込む
			 *
			 * 同じ型が既にあれば置き換える（components と addedComponents の両方にある型は、
			 * 後から丸ごと上書きする addedComponents の値が実行時の値なので、そちらを残す）。
			 */
			void LoadOverrideComponent(PrefabEditNode& node, const std::string& typeName, const util::JsonValue& value,
				const PrefabOverrideSource source)
			{
				ComponentRegistry& registry = ComponentRegistry::Get();
				const TypeInfo t = registry.TypeOf(typeName);
				if (t == TypeInfo()) { return; }                         // 未登録 typeName はスキップ

				PrefabEditComponent comp{ AlignedStorage(t) };
				const ComponentMeta* meta = registry.Find(t.GetHash());
				if (meta && meta->deserializePtr) {
					meta->deserializePtr(comp.data.Get(), value);
				}
				if (meta && meta->serializePtr) {
					meta->serializePtr(comp.data.Get(), comp.loadedJson);
				}
				comp.source = source;

				for (PrefabEditComponent& existing : node.components) {
					if (existing.data.Type() == t) {
						existing = std::move(comp);
						return;
					}
				}
				node.components.push_back(std::move(comp));
			}
		}


		util::JsonValue PrefabNodeToJson(const PrefabEditNode& node)
		{
			ComponentRegistry& registry = ComponentRegistry::Get();

			util::JsonValue obj = util::JsonValue::MakeObject();
			obj.Set("name", util::JsonValue(node.name));

			// 参照ノード: prefab パス + overrides（読み込んだ生 JSON に編集した差分だけを反映する）。
			// components/children の実体は解決時に参照先から展開され、overrides が上書き適用される（§7.3）。
			if (!node.prefabRef.empty()) {
				obj.Set("prefab", util::JsonValue(node.prefabRef));

				util::JsonValue overrides = BuildReferenceOverrides(node);
				if (!overrides.GetObject().empty()) { obj.Set("overrides", std::move(overrides)); }
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

			// 参照ノード: prefab パス + overrides を復元する。overrides の生 JSON は丸ごと控え、
			// 書き出しでは編集で変わったところだけを書き換える（name / children / removedChildren も保持される）。
			if (json.Contains("prefab") && json["prefab"].IsString()) {
				node->prefabRef = json["prefab"].AsString();

				if (json.Contains("overrides") && json["overrides"].IsObject()) {
					const util::JsonValue& ov = json["overrides"];
					node->overridesRaw = ov;

					// 名前は overrides.name を優先する（実行時の ApplyPatch と同じ優先順）
					if (ov.Contains(KEY_NAME) && ov[KEY_NAME].IsString()) { node->name = ov[KEY_NAME].AsString(); }

					// override コンポーネント（deep-merge）と追加コンポーネント（丸ごと上書き）。出どころを覚える
					if (ov.Contains(KEY_COMPONENTS) && ov[KEY_COMPONENTS].IsObject()) {
						for (const auto& kv : ov[KEY_COMPONENTS].GetObject()) {
							LoadOverrideComponent(*node, kv.first, kv.second, PrefabOverrideSource::Components);
						}
					}
					if (ov.Contains(KEY_ADDED_COMPONENTS) && ov[KEY_ADDED_COMPONENTS].IsObject()) {
						for (const auto& kv : ov[KEY_ADDED_COMPONENTS].GetObject()) {
							LoadOverrideComponent(*node, kv.first, kv.second, PrefabOverrideSource::AddedComponents);
						}
					}
					if (ov.Contains(KEY_REMOVED_COMPONENTS) && ov[KEY_REMOVED_COMPONENTS].IsArray()) {
						for (const util::JsonValue& r : ov[KEY_REMOVED_COMPONENTS].GetArray()) {
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


		namespace
		{
			/** root 自身か、その子孫に target があれば true */
			bool IsSelfOrDescendant(const PrefabEditNode* root, const PrefabEditNode* target)
			{
				if (!root || !target) { return false; }
				if (root == target) { return true; }
				for (const auto& child : root->children) {
					if (IsSelfOrDescendant(child.get(), target)) { return true; }
				}
				return false;
			}
		}


		void PrefabNodeDrawTree(const PrefabEditNode* node, const PrefabTreeDrawOptions& options, const int depth,
			std::vector<PrefabNodeOp>& ops)
		{
			constexpr int MAX_DEPTH = 64;
			if (!node || depth >= MAX_DEPTH) { return; }

			// 要求のポインタは非 const（呼び出し側が同じフレームで適用するため）。ここでは書き換えない。
			PrefabEditNode*  mutableNode = const_cast<PrefabEditNode*>(node);
			const bool       isRef       = !node->prefabRef.empty();

			ImGui::PushID(node);

			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
			                         | ImGuiTreeNodeFlags_SpanAvailWidth
			                         | ImGuiTreeNodeFlags_DefaultOpen;
			if (options.selected == node) { flags |= ImGuiTreeNodeFlags_Selected; }
			if (node->children.empty())   { flags |= ImGuiTreeNodeFlags_Leaf; }

			const char* mark   = (options.markReference && isRef) ? " [prefab]" : "";
			const bool  opened = ImGui::TreeNodeEx("##node", flags, "%s%s", node->name.c_str(), mark);
			if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
				ops.push_back(PrefabNodeOp{ PrefabNodeOpKind::Select, mutableNode, nullptr });
			}

			// ドラッグアンドドロップ: ノードを別のノードの子の末尾へ移す要求を返す
			if (options.enableMove) {
				if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
					ImGui::SetDragDropPayload(PREFAB_NODE_DRAG_PAYLOAD, &node, sizeof(node));
					ImGui::TextUnformatted(node->name.c_str());
					ImGui::EndDragDropSource();
				}
				if (ImGui::BeginDragDropTarget()) {
					const ImGuiPayload* peek = ImGui::GetDragDropPayload();
					bool acceptable = peek && peek->IsDataType(PREFAB_NODE_DRAG_PAYLOAD) && peek->DataSize == sizeof(node);
					if (acceptable) {
						const PrefabEditNode* dragged = *static_cast<const PrefabEditNode* const*>(peek->Data);
						if (IsSelfOrDescendant(dragged, node))      { acceptable = false; }   // 自分と子孫の下へは移さない
						if (isRef && !options.allowRefChild)        { acceptable = false; }
					}
					if (acceptable) {
						if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(PREFAB_NODE_DRAG_PAYLOAD)) {
							PrefabEditNode* dragged = *static_cast<PrefabEditNode* const*>(payload->Data);
							ops.push_back(PrefabNodeOp{ PrefabNodeOpKind::Move, dragged, mutableNode });
						}
					}
					ImGui::EndDragDropTarget();
				}
			}

			// 右クリック: 子追加 / 削除（どちらも要求を返すだけ）
			if (ImGui::BeginPopupContextItem()) {
				if ((!isRef || options.allowRefChild) && ImGui::MenuItem("Add Child")) {
					ops.push_back(PrefabNodeOp{ PrefabNodeOpKind::AddChild, mutableNode, nullptr });
				}
				if (node != options.root && ImGui::MenuItem("Delete")) {
					ops.push_back(PrefabNodeOp{ PrefabNodeOpKind::Delete, mutableNode, nullptr });
				}
				ImGui::EndPopup();
			}

			if (opened) {
				for (const auto& child : node->children) {
					PrefabNodeDrawTree(child.get(), options, depth + 1, ops);
				}
				ImGui::TreePop();
			}

			ImGui::PopID();
		}


		void PrefabNodeApplyOpsInPlace(const std::vector<PrefabNodeOp>& ops, PrefabEditNode*& selected,
			PrefabEditNode*& pendingDelete, const bool ensureTransform)
		{
			for (const PrefabNodeOp& op : ops) {
				if (!op.target) { continue; }
				switch (op.kind) {
				case PrefabNodeOpKind::Select:
					selected = op.target;
					break;

				case PrefabNodeOpKind::AddChild:
					{
						auto child  = std::make_unique<PrefabEditNode>();
						child->name = "Child";
						if (ensureTransform) { PrefabNodeEnsureTransform(*child); }
						selected = child.get();
						op.target->children.push_back(std::move(child));
					}
					break;

				case PrefabNodeOpKind::Delete:
					pendingDelete = op.target;
					// 削除するノードかその子孫が選択中だと、除去後に選択が解放済みを指すので外しておく
					if (IsSelfOrDescendant(op.target, selected)) { selected = nullptr; }
					break;

				case PrefabNodeOpKind::Move:
					break;   // その場適用の対象外（enableMove を使うのは Editor だけ）
				}
			}
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
