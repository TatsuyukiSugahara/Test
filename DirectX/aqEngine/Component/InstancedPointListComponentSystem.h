#pragma once
#include "ECS/ECS.h"
#include "Math/Vector.h"
#include <vector>


namespace aq
{
	namespace ecs
	{
		/**
		 * 1インスタンス分の配置情報。位置に加えて姿勢・非一様スケール・色を持てる。
		 * 空間はエンティティローカル(gather でエンティティのワールド行列が掛かる)。
		 */
		struct InstancePoint
		{
			aq::math::Vector3    position = aq::math::Vector3(0.0f, 0.0f, 0.0f);
			aq::math::Quaternion rotation = aq::math::Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
			aq::math::Vector3    scale    = aq::math::Vector3(1.0f, 1.0f, 1.0f);
			aq::math::Vector4    color    = aq::math::Vector4(1.0f, 1.0f, 1.0f, 1.0f);
		};




		/**
		 * InstancedStaticMeshComponent と同じエンティティに付ける「配置座標リスト」。
		 * RenderSystem の gather が、このリストの各座標に InstancedStaticMeshComponent の
		 * メッシュを1インスタンスずつ配置する(1エンティティが多数インスタンスを表す)。
		 * 座標は動的に追加/削除できる(ImGui などから)。
		 *
		 * 配置の指定方法は2通りで、instancePoints_ が非空ならそちらを優先する:
		 *   - instancePoints_: 点毎に位置・姿勢・非一様スケール・色を持つ(コード生成向け)。
		 *   - points_ + scale_: 位置のみ + 全点共通の一様スケール(色は白)。従来互換。
		 *
		 * TODO: 手入力に加えて、別途 JSON など外部データから配置座標を読み込む対応を予定。
		 */
		class InstancedPointListComponent : public aq::ecs::IComponent
		{
			ecsComponent(aq::ecs::InstancedPointListComponent);

		private:
			/** 位置のみの配置(従来互換) */
			std::vector<aq::math::Vector3> points_;
			float                          scale_ = 1.0f;   // 全点共通の一様スケール

			/** 姿勢・非一様スケール・色付きの配置(非空ならこちらを使う) */
			std::vector<InstancePoint>     instancePoints_;


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


			/**
			 * 姿勢・非一様スケール・色付きの配置
			 */
		public:
			/** 1点追加 */
			inline void AddInstancePoint(const InstancePoint& p) { instancePoints_.push_back(p); }
			/** 全消去 */
			inline void ClearInstancePoints()                    { instancePoints_.clear(); }
			/** 事前確保(大量生成時のバッファ再確保を避ける) */
			inline void ReserveInstancePoints(size_t n)          { instancePoints_.reserve(n); }

			inline std::vector<InstancePoint>&       InstancePoints()          { return instancePoints_; }
			inline const std::vector<InstancePoint>& GetInstancePoints() const { return instancePoints_; }
			inline size_t                            InstancePointCount() const { return instancePoints_.size(); }

			// 永続フィールドなし(座標はコード/ImGui から動的に構築する)。
			template <typename V>
			void Reflect(V&) {}
		};
	}
}
