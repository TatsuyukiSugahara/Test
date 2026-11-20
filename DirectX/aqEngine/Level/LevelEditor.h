#pragma once
#ifdef AQ_DEBUG_IMGUI
#include "Core/IDebugRenderable.h"
#include "ECS/PrefabEditor.h"     // ecs::PrefabEditNode
#include "Level/LevelEditModel.h" // 編集状態
#include "Level/LevelManager.h"   // LevelLoadHandle
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
		 * 編集状態は LevelEditModel が持ち、このパネルは UI の状態だけを持つ（エディタ全体設計 7.8 節「3.」）。
		 */
		class LevelEditorPanel : public IDebugRenderable
		{
		private:
			/** 編集モデル（.level.json に対応） */
			LevelEditModel       model_;

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

			void Save();
			void Load();
		};
	}
}
#endif
