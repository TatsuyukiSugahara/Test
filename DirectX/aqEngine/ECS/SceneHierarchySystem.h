#pragma once
#ifdef AQ_DEBUG_IMGUI
#include "ECS/ECS.h"
#include "ECS/Entity.h"

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

			void DebugRenderMenu() override;
			void DebugRender()     override;

			static SceneHierarchySystem& Get()         { return *instance_; }
			static bool                  IsAvailable() { return instance_ != nullptr; }

		private:
			void DrawHierarchyWindow();
			void DrawInspectorWindow();
			void DrawEntityNode(EntityHandle handle, int depth = 0);
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
