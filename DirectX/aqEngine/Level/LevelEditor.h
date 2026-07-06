#pragma once
#ifdef AQ_DEBUG_IMGUI
#include "Core/IDebugRenderable.h"
#include "ECS/PrefabEditor.h"   // ecs::PrefabEditNode
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
				std::string path;
				bool        loadOnStart = true;
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


		public:
			LevelEditorPanel();

			const char* GetDebugLabel()    const override { return "Level Editor"; }
			const char* GetDebugCategory() const override { return "Tools"; }
			void        DebugRenderMenu() override;
			void        DebugRender()     override;


		private:
			void NewLevel();

			void DrawToolbar();
			void DrawEntities();
			void DrawInspector();
			void DrawSubLevels();

			/** 編集モデル → .level.json 相当の JSON。Save / Load in World が共有する。 */
			util::JsonValue BuildJson() const;

			void Save();
			void Load();
			void LoadInWorld();   // 現在の編集内容を in-memory 登録 → LevelManager でワールドへロード
		};
	}
}
#endif
