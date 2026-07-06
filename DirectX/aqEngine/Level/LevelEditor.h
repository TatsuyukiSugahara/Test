#pragma once
#ifdef AQ_DEBUG_IMGUI
#include "Core/IDebugRenderable.h"
#include "ECS/PrefabEditor.h"   // ecs::PrefabEditNode
#include "Level/LevelManager.h" // LevelLoadHandle
#include <memory>
#include <string>
#include <vector>


namespace aq
{
	namespace level
	{
		/**
		 * .level.json を GUI で組み立てる Level エディタパネル。
		 * entities は Prefab フォレスト（各 root = ecs::PrefabEditNode ツリー）として編集し、
		 * ノード編集ロジック（ツリー/インスペクター/JSON 往復）は Prefab エディタと共有する
		 * （ecs::PrefabNode* オペレーション）。subLevels は path + loadOnStart のリストで持つ。
		 */
		class LevelEditorPanel : public IDebugRenderable
		{
		private:
			/** サブLevel エントリ（.level.json の subLevels[] に対応） */
			struct SubLevelEdit
			{
				bool                                              isInline = false;   // true = その場で作るインライン定義
				std::string                                       path;               // isInline=false: 外部 .level.json パス
				std::string                                       name = "SubLevel";  // isInline=true: 名前
				std::vector<std::unique_ptr<ecs::PrefabEditNode>> entities;           // isInline=true: 配下 entity ツリー
				bool                                              loadOnStart = true;
			};

			/** 編集モデル（.level.json に対応） */
			std::string                                       name_ = "Level";
			std::vector<std::unique_ptr<ecs::PrefabEditNode>> entities_;   // 各要素 = 1 トップレベル entity ツリー
			std::vector<SubLevelEdit>                         subLevels_;

			/** UI 状態 */
			ecs::PrefabEditNode* selected_      = nullptr;
			ecs::PrefabEditNode* pendingDelete_ = nullptr;
			bool                 show_          = false;
			char                 pathBuf_[260]  = "Assets/Levels/New.level.json";
			LevelLoadHandle      lastAsync_;                     // 直近の LoadAsync 進捗（バー表示用）


		public:
			LevelEditorPanel();

			const char* GetDebugLabel()    const override { return "Level Editor"; }
			const char* GetDebugCategory() const override { return "Tools"; }
			void        DebugRenderMenu() override;
			void        DebugRender()     override;


		private:
			void NewLevel();

			void DrawToolbar();
			void DrawInspector();
			void DrawSubLevels();

			/** entity リストを描画する（+ Entity / + Prefab ボタン + ツリー）。トップレベルとインライン sub-level で共有。 */
			void DrawEntityList(std::vector<std::unique_ptr<ecs::PrefabEditNode>>& list, const char* idLabel);

			/** list に entity を追加する。asPrefab=true なら prefabRef を持つ参照ノード、false なら Transform 必須の実体ノード。 */
			void AddEntityNode(std::vector<std::unique_ptr<ecs::PrefabEditNode>>& list, const bool asPrefab);

			/** list から pendingDelete_ を外す（トップレベル除去 or 木からの除去）。外したら true。 */
			bool RemovePendingFrom(std::vector<std::unique_ptr<ecs::PrefabEditNode>>& list);

			/** 編集モデル → .level.json 相当の JSON。Save / Load in World が共有する。 */
			util::JsonValue BuildJson() const;

			void Save();
			void Load();
			void LoadInWorld();   // 現在の編集内容を in-memory 登録 → LevelManager でワールドへロード
		};
	}
}
#endif
