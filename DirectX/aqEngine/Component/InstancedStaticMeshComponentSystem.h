#pragma once
#include "ECS/ECS.h"
#include "Graphics/InstancedStaticMesh.h"
#include <string>


namespace aq
{
	namespace ecs
	{
		/**
		 * 共有 InstancedStaticMesh のインスタンスとして描画されるエンティティに付けるコンポーネント。
		 * どの共有メッシュかは名前(例 "cube")で指定し、コード登録した名前レジストリから解決する
		 * (JSON でも "mesh":"cube" で指定可能)。ワールド行列は HierarchicalTransformComponent から取り、
		 * RenderSystem が gather して 1 メッシュぶんを1ドローで描く。
		 */
		class InstancedStaticMeshComponent : public aq::ecs::IComponent
		{
			ecsComponent(aq::ecs::InstancedStaticMeshComponent);

		private:
			std::string                          meshName_;          // 共有メッシュ名(JSON 永続)
			mutable graphics::InstancedStaticMesh* resolved_ = nullptr;  // 解決キャッシュ


		public:
			InstancedStaticMeshComponent()  {}
			~InstancedStaticMeshComponent() {}


		public:
			inline void SetMeshName(const char* name)
			{
				meshName_ = name ? name : "";
				resolved_ = nullptr;
			}
			inline const std::string& GetMeshName() const { return meshName_; }

			// 名前レジストリから共有メッシュを解決する(初回のみ引いてキャッシュ)。未登録なら nullptr。
			inline graphics::InstancedStaticMesh* GetMesh() const
			{
				if (!resolved_ && !meshName_.empty()) {
					resolved_ = graphics::InstancedStaticMesh::GetByName(meshName_.c_str());
				}
				return resolved_;
			}

			// 永続フィールド: メッシュ名(JSON 保存/読込)。文字列は FieldPath を使う。
			template <typename V>
			void Reflect(V& visitor)
			{
				visitor.FieldPath("mesh", meshName_, "mesh");
			}
		};
	}
}
