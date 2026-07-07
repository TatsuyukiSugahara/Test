#pragma once
#include "ECS/ECS.h"
#include "Math/Vector.h"
#include <vector>


namespace aq
{
	namespace ecs
	{
		/**
		 * InstancedStaticMeshComponent と同じエンティティに付ける「配置座標リスト」。
		 * RenderSystem の gather が、このリストの各座標に InstancedStaticMeshComponent の
		 * メッシュを1インスタンスずつ配置する(1エンティティが多数インスタンスを表す)。
		 * 座標は動的に追加/削除できる(ImGui などから)。scale は全点共通の一様スケール。
		 *
		 * TODO: 手入力に加えて、別途 JSON など外部データから配置座標を読み込む対応を予定。
		 */
		class InstancedPointListComponent : public aq::ecs::IComponent
		{
			ecsComponent(aq::ecs::InstancedPointListComponent);

		private:
			std::vector<aq::math::Vector3> points_;
			float                          scale_ = 1.0f;   // 全点共通の一様スケール


		public:
			InstancedPointListComponent()  {}
			~InstancedPointListComponent() {}


		public:
			inline void AddPoint(const aq::math::Vector3& p) { points_.push_back(p); }
			inline void Clear()                              { points_.clear(); }

			inline std::vector<aq::math::Vector3>&       Points()          { return points_; }
			inline const std::vector<aq::math::Vector3>& GetPoints() const { return points_; }
			inline size_t                                Count() const     { return points_.size(); }

			inline float  GetScale() const   { return scale_; }
			inline void   SetScale(float s)  { scale_ = s; }

			// 永続フィールドなし(座標はコード/ImGui から動的に構築する)。
			template <typename V>
			void Reflect(V&) {}
		};
	}
}
