#include "aq.h"
#ifdef AQ_DEBUG_IMGUI
#include "Level/LevelEditModel.h"
#include "Level/LevelManager.h"
#include "Level/LevelRegistry.h"
#include "Level/LevelSerializer.h"
#include "ECS/EntityContext.h"
#include "ECS/PrefabEditNodeOps.h"
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


		void LevelEditModel::ProcessPreviewRequest()
		{
			if (!previewRequested_) { return; }
			previewRequested_ = false;

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
