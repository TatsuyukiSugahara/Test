#pragma once
#include "ECS/System.h"


namespace aq
{
	namespace ecs
	{
		/**
		 * パーティクル更新システム。
		 *
		 * ParticleEmitterComponent を持つ全エンティティを走査し、同居する
		 * HierarchicalTransformComponent のワールド位置を原点として CPU
		 * シミュレーションを 1 フレームぶん進める (仕様 §5)。
		 * ワールド変換確定後に走らせるため HierarcicalTransformSystem に依存させること。
		 * 描画データ生成は RenderSystem::BuildRenderFrame 側が runtimes を読んで行う。
		 */
		class ParticleSystem : public aq::ecs::SystemBase
		{
		public:
			void Update() override;
		};
	}
}
