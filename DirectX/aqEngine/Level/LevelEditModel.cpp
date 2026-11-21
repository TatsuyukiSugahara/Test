#include "aq.h"
#ifdef AQ_DEBUG_IMGUI
#include "Level/LevelEditModel.h"
#include "Level/LevelManager.h"
#include "Level/LevelRegistry.h"
#include "Level/LevelSerializer.h"
#include "ECS/EntityContext.h"
#include "ECS/PrefabEditNodeOps.h"
#include "ECS/PrefabSerializer.h"
#include "ECS/ComponentRegistry.h"
#include <algorithm>
#include <filesystem>
#include <system_error>


namespace aq
{
	namespace level
	{
		namespace
		{
			/**
			 * プレビューの in-memory 登録キーの通し番号
			 *
			 * LevelRegistry は全体で 1 つなので、モデルが複数あってもキーが重ならないようにモデルの外に置く
			 * （Register は既存キーがあると古いデータを返してしまう）。
			 */
			uint32_t previewCounter = 0;


			/** path のディレクトリ（区切りは '/' に正規化済みのこと。LevelSerializer::Load の基準と同じ） */
			std::string DirOf(const std::string& path)
			{
				const size_t pos = path.find_last_of('/');
				return (pos == std::string::npos) ? std::string() : path.substr(0, pos);
			}


			util::JsonValue EntitiesToJson(const std::vector<std::unique_ptr<ecs::PrefabEditNode>>& entities)
			{
				util::JsonValue out = util::JsonValue::MakeArray();
				for (const auto& entity : entities) {
					out.PushBack(ecs::PrefabNodeToJson(*entity));
				}
				return out;
			}


			void EntitiesFromJson(const util::JsonValue& level, std::vector<std::unique_ptr<ecs::PrefabEditNode>>& out)
			{
				// entity ノードは PrefabNodeFromJson が "prefab" を prefabRef として復元する（参照ノードもそのまま往復）。
				if (!level.Contains("entities") || !level["entities"].IsArray()) { return; }
				for (const util::JsonValue& node : level["entities"].GetArray()) {
					out.push_back(ecs::PrefabNodeFromJson(node));
				}
			}


			util::JsonValue SubLevelsToJson(const std::vector<SubLevelEdit>& subLevels)
			{
				util::JsonValue subs = util::JsonValue::MakeArray();
				for (const SubLevelEdit& sub : subLevels) {
					util::JsonValue obj = util::JsonValue::MakeObject();
					if (sub.isInline) {
						// インライン定義: level を { name, entities, subLevels } オブジェクトとして埋め込む。
						// 入れ子の subLevels はランタイム（LevelSerializer）が再帰で読むので、捨てずに書き戻す。
						util::JsonValue lvl = util::JsonValue::MakeObject();
						lvl.Set("name", util::JsonValue(sub.name));
						lvl.Set("entities", EntitiesToJson(sub.entities));
						if (!sub.subLevels.empty()) {
							lvl.Set("subLevels", SubLevelsToJson(sub.subLevels));
						}
						obj.Set("level", std::move(lvl));
					} else {
						// 外部ファイル参照: level を文字列パスにする。
						obj.Set("level", util::JsonValue(sub.path));
					}
					obj.Set("loadOnStart", util::JsonValue(sub.loadOnStart));
					subs.PushBack(std::move(obj));
				}
				return subs;
			}


			void SubLevelsFromJson(const util::JsonValue& level, std::vector<SubLevelEdit>& out)
			{
				if (!level.Contains("subLevels") || !level["subLevels"].IsArray()) { return; }
				for (const util::JsonValue& sub : level["subLevels"].GetArray()) {
					if (!sub.IsObject() || !sub.Contains("level")) { continue; }
					SubLevelEdit edit;
					edit.loadOnStart = sub.Contains("loadOnStart") ? sub["loadOnStart"].AsBool(true) : true;

					const util::JsonValue& lv = sub["level"];
					if (lv.IsObject()) {
						// インライン定義: name + entities + 入れ子の subLevels を復元する。
						edit.isInline = true;
						edit.name     = lv.Contains("name") ? lv["name"].AsString() : std::string("SubLevel");
						EntitiesFromJson(lv, edit.entities);
						SubLevelsFromJson(lv, edit.subLevels);
					} else if (lv.IsString()) {
						// 外部ファイル参照。
						edit.isInline = false;
						edit.path     = lv.AsString();
					} else {
						continue;
					}
					out.push_back(std::move(edit));
				}
			}


			ecs::PrefabEditNode* FindNodeIn(std::vector<std::unique_ptr<ecs::PrefabEditNode>>& nodes, const uint32_t id)
			{
				for (auto& node : nodes) {
					if (node->editId == id) { return node.get(); }
					if (ecs::PrefabEditNode* found = FindNodeIn(node->children, id)) { return found; }
				}
				return nullptr;
			}


			ecs::PrefabEditNode* FindNodeInSubLevels(std::vector<SubLevelEdit>& subLevels, const uint32_t id)
			{
				for (SubLevelEdit& sub : subLevels) {
					if (!sub.isInline) { continue; }   // 外部ファイル参照は entity ツリーを持たない
					if (ecs::PrefabEditNode* found = FindNodeIn(sub.entities, id)) { return found; }
					if (ecs::PrefabEditNode* found = FindNodeInSubLevels(sub.subLevels, id)) { return found; }
				}
				return nullptr;
			}

			using NodeList = std::vector<std::unique_ptr<ecs::PrefabEditNode>>;


			/** ノードの置き場所（親の列と添字） */
			struct NodeLocation
			{
				NodeList*  list = nullptr;
				size_t     index = 0;
				NodeParent parent;
			};


			/** list 以下（子孫を含む）から id を探す。owner は list を持つノード（ルート列なら nullptr） */
			bool LocateInList(NodeList& list, const uint32_t id, const ecs::PrefabEditNode* owner, const int subLevelIndex, NodeLocation& out)
			{
				for (size_t i = 0; i < list.size(); ++i) {
					ecs::PrefabEditNode* node = list[i].get();
					if (node->editId == id) {
						out.list  = &list;
						out.index = i;
						if (owner) {
							out.parent = NodeParent{ owner->editId, -1 };
						} else {
							out.parent = NodeParent{ 0, subLevelIndex };
						}
						return true;
					}
					if (LocateInList(node->children, id, node, subLevelIndex, out)) { return true; }
				}
				return false;
			}


			/** トップレベルと 1 段目のインライン subLevel のルート列から id を探す（UI と同じく 2 段目以降は見ない） */
			bool Locate(NodeList& entities, std::vector<SubLevelEdit>& subLevels, const uint32_t id, NodeLocation& out)
			{
				if (id == 0) { return false; }
				if (LocateInList(entities, id, nullptr, -1, out)) { return true; }
				for (size_t s = 0; s < subLevels.size(); ++s) {
					if (!subLevels[s].isInline) { continue; }
					if (LocateInList(subLevels[s].entities, id, nullptr, static_cast<int>(s), out)) { return true; }
				}
				return false;
			}


			/** parent が指す、挿入先の列。参照ノードの下・ファイル参照の subLevel・存在しない親は nullptr。outOwner は親ノード（ルート列なら nullptr） */
			NodeList* ResolveTargetList(NodeList& entities, std::vector<SubLevelEdit>& subLevels, const NodeParent& parent, ecs::PrefabEditNode** outOwner)
			{
				if (outOwner) { *outOwner = nullptr; }

				if (parent.nodeId != 0) {
					NodeLocation loc;
					if (!Locate(entities, subLevels, parent.nodeId, loc)) { return nullptr; }
					ecs::PrefabEditNode* owner = (*loc.list)[loc.index].get();
					if (!owner->prefabRef.empty()) { return nullptr; }   // 参照ノードの下には置かない
					if (outOwner) { *outOwner = owner; }
					return &owner->children;
				}

				if (parent.subLevelIndex < 0) { return &entities; }
				if (static_cast<size_t>(parent.subLevelIndex) >= subLevels.size()) { return nullptr; }
				SubLevelEdit& sub = subLevels[static_cast<size_t>(parent.subLevelIndex)];
				if (!sub.isInline) { return nullptr; }   // ファイル参照の subLevel はノードを持たない
				return &sub.entities;
			}


			/** root 自身か、その子孫に target があれば true */
			bool IsSelfOrDescendant(const ecs::PrefabEditNode* root, const ecs::PrefabEditNode* target)
			{
				if (root == target) { return true; }
				for (const auto& child : root->children) {
					if (IsSelfOrDescendant(child.get(), target)) { return true; }
				}
				return false;
			}


			/** PrefabSerializer の JoinPath と同じ規則で、絶対パスなら true */
			bool IsAbsolutePath(std::string path)
			{
				for (char& c : path) { if (c == '\\') { c = '/'; } }
				return (!path.empty() && path[0] == '/') || (path.size() >= 2 && path[1] == ':');
			}


			bool NodesHaveRelativeReference(const NodeList& nodes)
			{
				for (const auto& node : nodes) {
					if (!node->prefabRef.empty() && !IsAbsolutePath(node->prefabRef)) { return true; }
					if (NodesHaveRelativeReference(node->children)) { return true; }
				}
				return false;
			}


			bool SubLevelsHaveRelativeReference(const std::vector<SubLevelEdit>& subLevels)
			{
				for (const SubLevelEdit& sub : subLevels) {
					if (sub.isInline) {
						if (NodesHaveRelativeReference(sub.entities)) { return true; }
						if (SubLevelsHaveRelativeReference(sub.subLevels)) { return true; }
					} else if (!IsAbsolutePath(sub.path)) {
						return true;
					}
				}
				return false;
			}


			/** PrefabSerializer の JoinPath と同じ規則で、dir 基準の rel を結合・正規化する（絶対パスなら rel のまま） */
			std::string JoinReferencePath(const std::string& dir, const std::string& rel)
			{
				std::string relative = rel;
				for (char& c : relative) { if (c == '\\') { c = '/'; } }
				if (IsAbsolutePath(relative) || dir.empty()) { return relative; }

				std::string joined = dir + "/" + relative;
				for (char& c : joined) { if (c == '\\') { c = '/'; } }
				return joined;
			}


			/** 参照ノードの JSON を PrefabSerializer で解決し、根の components を outComponents へ返す */
			bool ResolveComponentsOf(const util::JsonValue& nodeJson, const std::string& baseDir, util::JsonValue& outComponents)
			{
				const ecs::Prefab prefab = ecs::PrefabSerializer::FromJson(nodeJson, baseDir.c_str());
				if (!prefab.IsValid()) { return false; }
				outComponents = prefab.Data()->root.components;
				return true;
			}


			/** 参照ノードの JSON のうち、overrides を読み込んだときの生 JSON（overridesRaw）に差し替えたもの */
			util::JsonValue MakeRawReferenceJson(const ecs::PrefabEditNode& node)
			{
				util::JsonValue json = util::JsonValue::MakeObject();
				json.Set("name", util::JsonValue(node.name));
				json.Set("prefab", util::JsonValue(node.prefabRef));
				if (node.overridesRaw.IsObject() && !node.overridesRaw.GetObject().empty()) {
					json.Set("overrides", node.overridesRaw);
				}
				return json;
			}


			ecs::PrefabEditComponent* FindEditComponent(ecs::PrefabEditNode& node, const ecs::TypeInfo& type)
			{
				for (ecs::PrefabEditComponent& component : node.components) {
					if (component.data.Type() == type) { return &component; }
				}
				return nullptr;
			}


			void SetError(std::string* error, const std::string& message)
			{
				if (error) { *error = message; }
			}
		}


		LevelEditModel::LevelEditModel()
		{
			NewLevel();
		}


		void LevelEditModel::NewLevel()
		{
			name_ = "Level";
			entities_.clear();
			subLevels_.clear();
			baseDir_.clear();

			// 最初の空エンティティを 1 つ用意する（Transform 必須）。
			auto entity  = std::make_unique<ecs::PrefabEditNode>();
			entity->name = "Entity";
			ecs::PrefabNodeEnsureTransform(*entity);
			AssignId(*entity);
			entities_.push_back(std::move(entity));
		}


		bool LevelEditModel::LoadFile(const std::string& path)
		{
			const util::JsonValue root = util::JsonParser::ParseFile(path.c_str());
			if (root.IsNull()) { return false; }
			if (!FromJson(root)) { return false; }

			baseDir_ = DirOf(LevelRegistry::Normalize(path));
			return true;
		}


		bool LevelEditModel::SaveFile(const std::string& path)
		{
			namespace fs = std::filesystem;

			// 書き込み中に落ちても元のファイルが壊れないよう、一時ファイルに書いてから置き換える。
			const std::string tempPath = path + ".tmp";
			if (!util::JsonSerializer::WriteFile(tempPath.c_str(), ToJson())) {
				std::error_code removeError;
				fs::remove(fs::path(tempPath), removeError);
				return false;
			}

			std::error_code renameError;
			fs::rename(fs::path(tempPath), fs::path(path), renameError);
			if (renameError) {
				std::error_code removeError;
				fs::remove(fs::path(tempPath), removeError);
				return false;
			}

			baseDir_ = DirOf(LevelRegistry::Normalize(path));
			return true;
		}


		util::JsonValue LevelEditModel::ToJson() const
		{
			util::JsonValue root = util::JsonValue::MakeObject();
			root.Set("name", util::JsonValue(name_));
			root.Set("entities", EntitiesToJson(entities_));
			if (!subLevels_.empty()) {
				root.Set("subLevels", SubLevelsToJson(subLevels_));
			}
			return root;
		}


		bool LevelEditModel::FromJson(const util::JsonValue& root)
		{
			if (!root.IsObject()) { return false; }

			// 一時へ読み込んでから入れ替える（途中で失敗してもモデルを変えないため）。
			std::vector<std::unique_ptr<ecs::PrefabEditNode>> entities;
			std::vector<SubLevelEdit>                         subLevels;
			EntitiesFromJson(root, entities);
			SubLevelsFromJson(root, subLevels);

			name_      = root.Contains("name") ? root["name"].AsString() : std::string("Level");
			entities_  = std::move(entities);
			subLevels_ = std::move(subLevels);

			// ノード ID を振る（読み込みのたび。保存しないので JSON 側には無い）。
			for (auto& entity : entities_) { AssignId(*entity); }
			AssignIdsInSubLevels(subLevels_);
			return true;
		}


		ecs::PrefabEditNode* LevelEditModel::FindNode(const uint32_t id)
		{
			if (id == 0) { return nullptr; }
			if (ecs::PrefabEditNode* found = FindNodeIn(entities_, id)) { return found; }
			return FindNodeInSubLevels(subLevels_, id);
		}


		const ecs::PrefabEditNode* LevelEditModel::FindNode(const uint32_t id) const
		{
			return const_cast<LevelEditModel*>(this)->FindNode(id);
		}


		bool LevelEditModel::FindParent(const uint32_t id, NodeParent& outParent, size_t& outIndex) const
		{
			LevelEditModel* self = const_cast<LevelEditModel*>(this);
			NodeLocation    loc;
			if (!Locate(self->entities_, self->subLevels_, id, loc)) { return false; }
			outParent = loc.parent;
			outIndex  = loc.index;
			return true;
		}


		uint32_t LevelEditModel::InsertNode(const NodeParent& parent, const size_t index, std::unique_ptr<ecs::PrefabEditNode> node)
		{
			if (!node) { return 0; }

			NodeList* list = ResolveTargetList(entities_, subLevels_, parent, nullptr);
			if (!list) { return 0; }

			ecs::PrefabEditNode* raw = node.get();
			AssignId(*raw);   // 採番済みの ID は変えず、未採番のノードだけに振る
			const size_t pos = (index < list->size()) ? index : list->size();
			list->insert(list->begin() + static_cast<std::ptrdiff_t>(pos), std::move(node));
			return raw->editId;
		}


		std::unique_ptr<ecs::PrefabEditNode> LevelEditModel::DetachNode(const uint32_t id, NodeParent* outParent, size_t* outIndex)
		{
			NodeLocation loc;
			if (!Locate(entities_, subLevels_, id, loc)) { return nullptr; }

			if (outParent) { *outParent = loc.parent; }
			if (outIndex)  { *outIndex  = loc.index; }

			std::unique_ptr<ecs::PrefabEditNode> node = std::move((*loc.list)[loc.index]);
			loc.list->erase(loc.list->begin() + static_cast<std::ptrdiff_t>(loc.index));
			return node;
		}


		bool LevelEditModel::MoveNode(const uint32_t id, const NodeParent& newParent, const size_t index)
		{
			NodeLocation src;
			if (!Locate(entities_, subLevels_, id, src)) { return false; }
			const ecs::PrefabEditNode* moving = (*src.list)[src.index].get();

			// 取り外す前に、行き先が有効かをすべて確かめる（失敗してもモデルを変えないため）。
			ecs::PrefabEditNode* owner = nullptr;
			NodeList*            dest  = ResolveTargetList(entities_, subLevels_, newParent, &owner);
			if (!dest) { return false; }
			if (owner && IsSelfOrDescendant(moving, owner)) { return false; }   // 自分と子孫の下へは移さない

			// 取り外してから挿入する。行き先の列は親ノードの持ち物か最上位の列なので、取り外しで無効にならない。
			std::unique_ptr<ecs::PrefabEditNode> node = std::move((*src.list)[src.index]);
			src.list->erase(src.list->begin() + static_cast<std::ptrdiff_t>(src.index));

			const size_t pos = (index < dest->size()) ? index : dest->size();   // 同じ列の中なら取り外した後の添字
			dest->insert(dest->begin() + static_cast<std::ptrdiff_t>(pos), std::move(node));
			return true;
		}


		bool LevelEditModel::SetNodeName(const uint32_t id, const std::string& name)
		{
			ecs::PrefabEditNode* node = FindNode(id);
			if (!node) { return false; }
			node->name = name;
			return true;
		}


		bool LevelEditModel::HasRelativeReferences() const
		{
			return NodesHaveRelativeReference(entities_) || SubLevelsHaveRelativeReference(subLevels_);
		}


		bool LevelEditModel::ResolveReferenceComponents(const uint32_t id, util::JsonValue& outComponents, std::string* error) const
		{
			const ecs::PrefabEditNode* node = FindNode(id);
			if (!node || node->prefabRef.empty()) {
				SetError(error, "参照ノードではない");
				return false;
			}

			// 解決の結果が空でも失敗とは限らない（途中の失敗は ResolveNode がログだけで空を返す）ので、直接の参照先だけを先に確かめる
			const std::string refPath = JoinReferencePath(baseDir_, node->prefabRef);
			if (util::JsonParser::ParseFile(refPath.c_str()).IsNull()) {
				SetError(error, "参照先を読めない: " + refPath);
				return false;
			}

			util::JsonValue resolved;
			if (!ResolveComponentsOf(ecs::PrefabNodeToJson(*node), baseDir_, resolved)) {
				SetError(error, "参照先を解決できない: " + refPath);
				return false;
			}
			outComponents = std::move(resolved);
			return true;
		}


		bool LevelEditModel::SetComponentField(const uint32_t id, const std::string& typeName, const std::string& key, const util::JsonValue& value)
		{
			ecs::ComponentRegistry& registry = ecs::ComponentRegistry::Get();
			const ecs::TypeInfo     type     = registry.TypeOf(typeName);
			if (type == ecs::TypeInfo()) { return false; }
			const ecs::ComponentMeta* meta = registry.Find(type.GetHash());
			if (!meta || !meta->serializePtr || !meta->deserializePtr) { return false; }

			ecs::PrefabEditNode* node = FindNode(id);
			if (!node) { return false; }

			util::JsonValue patch = util::JsonValue::MakeObject();
			patch.Set(key, value);

			ecs::PrefabEditComponent* existing = FindEditComponent(*node, type);

			// 実体ノード: 持っているコンポーネントのキーだけを変える
			if (node->prefabRef.empty()) {
				if (!existing) { return false; }
				meta->deserializePtr(existing->data.Get(), patch);
				return true;
			}

			// 参照ノード: 参照先から除かれた型は、解決後の値が無い
			if (std::find(node->removedComponents.begin(), node->removedComponents.end(), typeName) != node->removedComponents.end()) {
				return false;
			}

			// 付け替え済み、または新しく足した（loadedJson が空 = 全フィールドを書く）コンポーネントはそのまま変える
			if (existing && (existing->rebased || existing->loadedJson.IsNull())) {
				meta->deserializePtr(existing->data.Get(), patch);
				return true;
			}

			// 付け替え: R_cur（今の編集内容で解決した値）と R_raw（読み込んだ override だけで解決した値）を作る
			util::JsonValue current;
			if (!ResolveReferenceComponents(id, current, nullptr)) { return false; }
			if (!current.IsObject() || !current.Contains(typeName)) { return false; }

			util::JsonValue raw;
			if (!ResolveComponentsOf(MakeRawReferenceJson(*node), baseDir_, raw)) { return false; }
			if (!raw.IsObject() || !raw.Contains(typeName)) { return false; }

			// 失敗してもモデルを変えないよう、作り終えてから差し替える
			ecs::PrefabEditComponent rebased{ ecs::AlignedStorage(type) };
			{
				ecs::AlignedStorage base(type);
				meta->deserializePtr(base.Get(), raw[typeName]);
				meta->serializePtr(base.Get(), rebased.loadedJson);
			}
			meta->deserializePtr(rebased.data.Get(), current[typeName]);
			meta->deserializePtr(rebased.data.Get(), patch);
			rebased.source  = existing ? existing->source : ecs::PrefabOverrideSource::Components;
			rebased.rebased = true;

			if (existing) {
				*existing = std::move(rebased);
			} else {
				node->components.push_back(std::move(rebased));
			}
			return true;
		}


		bool LevelEditModel::AddComponent(const uint32_t id, const std::string& typeName, const util::JsonValue* json)
		{
			ecs::ComponentRegistry& registry = ecs::ComponentRegistry::Get();
			const ecs::TypeInfo     type     = registry.TypeOf(typeName);
			if (type == ecs::TypeInfo()) { return false; }
			const ecs::ComponentMeta* meta = registry.Find(type.GetHash());
			if (!meta) { return false; }

			ecs::PrefabEditNode* node = FindNode(id);
			if (!node || !node->prefabRef.empty()) { return false; }
			if (FindEditComponent(*node, type)) { return false; }

			ecs::PrefabEditComponent component{ ecs::AlignedStorage(type) };
			if (json && meta->deserializePtr) {
				meta->deserializePtr(component.data.Get(), *json);
			}
			node->components.push_back(std::move(component));
			return true;
		}


		bool LevelEditModel::RemoveComponent(const uint32_t id, const std::string& typeName, util::JsonValue* outJson)
		{
			ecs::ComponentRegistry& registry = ecs::ComponentRegistry::Get();
			const ecs::TypeInfo     type     = registry.TypeOf(typeName);
			if (type == ecs::TypeInfo()) { return false; }
			const ecs::ComponentMeta* meta = registry.Find(type.GetHash());
			if (!meta || !meta->addable) { return false; }   // Transform などは消さない

			ecs::PrefabEditNode* node = FindNode(id);
			if (!node || !node->prefabRef.empty()) { return false; }

			for (size_t i = 0; i < node->components.size(); ++i) {
				if (!(node->components[i].data.Type() == type)) { continue; }
				if (outJson) {
					*outJson = util::JsonValue();
					if (meta->serializePtr) { meta->serializePtr(node->components[i].data.Get(), *outJson); }
				}
				node->components.erase(node->components.begin() + static_cast<std::ptrdiff_t>(i));
				return true;
			}
			return false;
		}


		void LevelEditModel::AssignId(ecs::PrefabEditNode& node)
		{
			if (node.editId == 0) { node.editId = nextEditId_++; }
			for (auto& child : node.children) { AssignId(*child); }
		}


		void LevelEditModel::AssignIdsInSubLevels(std::vector<SubLevelEdit>& subLevels)
		{
			for (SubLevelEdit& sub : subLevels) {
				if (!sub.isInline) { continue; }   // 外部ファイル参照は entity ツリーを持たない
				for (auto& entity : sub.entities) { AssignId(*entity); }
				AssignIdsInSubLevels(sub.subLevels);
			}
		}


		void LevelEditModel::RequestPreview()
		{
			// 置き換えは完了待ちを伴うので、同じフレームに何回要求されても 1 回にまとめる（最後の編集内容で作る）。
			previewRequested_ = true;
		}


		void LevelEditModel::ReleasePreview()
		{
			LevelManager&       levelManager = LevelManager::Get();
			ecs::EntityContext& ctx          = ecs::EntityContext::Get();

			// 1. 次の Flush は予約済みの破棄（同じフレームの自動リロードが予約したものなど）も実行するので、
			//    その前に CPU と GPU の完了を待つ。直列構成でもレンダースレッドの完了だけでは GPU が前フレームの
			//    バッファを使っていることがある（LevelManager::ReloadAll と同じ理由）。
			aq::Engine::Get().WaitForRenderIdle();

			// 2. 予約済みのコマンドをすべて実行する。前回のプレビューの同期 Load の生成もここで実体化する
			//    （生成前にアンロードすると、まだ存在しないエンティティを拾えないため）。
			ctx.FlushPendingCommands();

			// 3. 前回のプレビューの Level をアンロードする（破棄は予約される。既にアンロード済みなら何もしない）。
			if (previewLevelId_.IsValid()) {
				levelManager.Unload(previewLevelId_);
			}

			// 4. 破棄を確定する。1 から描画を挟んでいないので、GPU の完了待ちは要らない。
			ctx.FlushPendingCommands();

			// 5. 前回の in-memory 登録を外す（プレビューの登録は常に 1 つだけにする）。
			if (!previewKey_.empty()) {
				LevelRegistry::Get().Unregister(previewKey_);
			}
			previewLevelId_ = LevelId();
			previewKey_.clear();
		}


		bool LevelEditModel::ClearPreview()
		{
			previewRequested_ = false;
			const bool hadPreview = previewLevelId_.IsValid() || !previewKey_.empty();
			ReleasePreview();
			return hadPreview;
		}


		void LevelEditModel::ProcessPreviewRequest()
		{
			if (!previewRequested_) { return; }
			previewRequested_ = false;

			ReleasePreview();

			LevelManager& levelManager = LevelManager::Get();

			// 6. 編集モデルから LevelData を作り、新しいキーで登録して Load する。
			//    相対パスは通常のファイルロードと同じく、開いたファイルのディレクトリから解決する。
			//    生成は次フレームの Flush で行われる。
			const util::JsonValue            root = ToJson();
			std::shared_ptr<const LevelData> data = LevelSerializer::FromJson(root, baseDir_.c_str());
			if (!data) { return; }

			const std::string key = "<level-editor-preview-" + std::to_string(previewCounter++) + ">";
			LevelRegistry::Get().Register(key, data);

			// 7. 新しい LevelId とキーを記録する。
			previewLevelId_ = levelManager.Load(key);
			previewKey_     = key;
		}
	}
}
#endif
