#pragma once
#ifdef AQ_DEBUG_IMGUI
#include "ECS/ECS.h"
#include "ECS/Entity.h"
#include <vector>

namespace aq
{
	namespace ecs
	{
		class SceneHierarchySystem : public SystemBase
		{
		public:
			SceneHierarchySystem();
			~SceneHierarchySystem();

			void Update() override {}

			RunModeMask GetRunModes() const override { return RunModeMask::All; }

			void DebugRenderMenu() override;
			void DebugRender()     override;

			static SceneHierarchySystem& Get()         { return *instance_; }
			static bool                  IsAvailable() { return instance_ != nullptr; }

			/**
			 * Hierarchy の中身を描く（ウィンドウは開かない。呼び出し側が Begin / End を持つ）。
			 * allowStructuralEdit が false のときは、エンティティの作成・親の付け替え・並べ替え・破棄を出さない。
			 */
			void DrawHierarchyContents(bool allowStructuralEdit);

			/**
			 * Inspector の中身を描く（ウィンドウは開かない）。
			 * allowStructuralEdit が false のときは、コンポーネントの追加・削除とエンティティの破棄を出さない。
			 * 名前と各コンポーネントの値の編集は常に出す。
			 */
			void DrawInspectorContents(bool allowStructuralEdit);

		private:
			void DrawHierarchyWindow();
			void DrawInspectorWindow();
			void DrawEntityNode(EntityHandle handle, bool allowStructuralEdit, int depth = 0);
			// 子を持たないエンティティ列を ImGuiListClipper で可視行のみ描画する（大量のフラット表示を軽量化）。
			void DrawFlatList(const std::vector<EntityHandle>& handles, bool allowStructuralEdit);
			void DrawInsertGap(EntityHandle parent, int insertIndex);
			void MoveChildToIndex(EntityHandle parent, EntityHandle dragged, int insertIndex);
			void DestroySubtree(EntityHandle handle);

			EntityHandle selectedHandle_;
			bool         showHierarchy_  = true;
			bool         showInspector_  = true;
			bool         destroySubtree_ = false;

			static SceneHierarchySystem* instance_;
		};
	}
}
#endif
