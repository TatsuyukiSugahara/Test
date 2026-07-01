#pragma once
#include "ECS/ECS.h"
#include "PrefabRegistry.h"
#include <string>

namespace aq
{
	namespace ecs
	{
		// 一定間隔で参照 Prefab をスポーンするコンポーネント（設計 §8.2）。
		//
		// Prefab の参照は「prefabPath（文字列）を正本に持ち、実行時に
		// PrefabRegistry::Resolve(path)→Find(id)→Prefab::Instantiate で解決する」だけ。
		// 専用の参照コンポーネントは設けない — 任意の自作コンポーネントが同じ手順で
		// Prefab を参照できる（SpawnerComponent はその一例）。
		struct SpawnerComponent : public IComponent
		{
			ecsComponent(aq::ecs::SpawnerComponent);

			std::string prefabPath;        // 正本（serialize される）
			PrefabId    resolved;          // ランタイム解決（serialize しない）
			float       interval = 1.0f;   // スポーン間隔（秒）
			int         maxCount = -1;      // 最大スポーン数（-1 = 無制限）
			float       timer    = 0.0f;   // ランタイム（serialize しない）
			int         spawned  = 0;      // ランタイム累計（serialize しない）

			template <typename V>
			void Reflect(V& visitor)
			{
				visitor.FieldPath("prefab", prefabPath, "Prefab");
				visitor.Field("interval", interval, "Interval (sec)");
				visitor.Field("maxCount", maxCount, "Max Count (-1=inf)");
			}
		};


		// SpawnerComponent を走査し、interval ごとに参照 Prefab を遅延生成する System。
		// 遅延生成（Prefab::Instantiate）は commandMutex_ のみ取得するため ForEach 中でも安全。
		class SpawnSystem : public SystemBase
		{
		public:
			void Update() override;

#ifdef AQ_DEBUG_IMGUI
			const char* GetDebugGroup()    const override { return "Prefab"; }
			const char* GetDebugTabLabel() const override { return "Spawner"; }
			void        RenderContent()          override;
#endif
		};
	}
}
