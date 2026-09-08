#pragma once
#include "ECS/ECS.h"
#include "Math/Vector.h"
#include "Graphics/InstancedStaticMesh.h"   // ベイク済み配置が持つ InstanceData
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
		 * ベイク済み静的インスタンスの 1 セル(XZ グリッド)。
		 * bounds を視錐台・距離判定に掛け、通ったセルだけをブロック一括で積む。
		 */
		struct BakedCell
		{
			aq::math::AABB bounds;        // メッシュの大きさを含むワールド空間 AABB
			uint32_t       offset = 0;    // instances 内の先頭位置
			uint32_t       count  = 0;    // このセルのインスタンス数
		};




		/**
		 * ベイク済み静的インスタンス一式(instances は cells の順に連続配置)。
		 * 連続配置なので、生存セルは offset/count でそのまま memcpy できる。
		 */
		struct BakedData
		{
			std::vector<aq::graphics::InstancedStaticMesh::InstanceData> instances;
			std::vector<BakedCell>                                       cells;
		};




		/**
		 * InstancedStaticMeshComponent と同じエンティティに付ける「配置座標リスト」。
		 * RenderSystem の gather が、このリストの各座標に InstancedStaticMeshComponent の
		 * メッシュを1インスタンスずつ配置する(1エンティティが多数インスタンスを表す)。
		 * 座標は動的に追加/削除できる(ImGui などから)。
		 *
		 * 配置の指定方法は3通りで、baked_ → instancePoints_ → points_ の順に優先する:
		 *   - baked_: 行列を事前計算しセルへ並べ替えた静的配置。毎フレームの判定がセル単位になる。
		 *   - instancePoints_: 点毎に位置・姿勢・非一様スケール・色を持つ(コード生成向け)。
		 *   - points_ + scale_: 位置のみ + 全点共通の一様スケール(色は白)。従来互換。
		 *
		 * ベイクはエンティティのワールド行列を織り込んだ状態で行うため、ベイク済みの
		 * エンティティは以後動かしてはいけない(動かす用途は instancePoints_ 経路を使う)。
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

			/** ベイク済み静的配置(非空ならこちらを最優先) */
			BakedData                      baked_;
			float                          maxDrawDistance_ = 0.0f;   // カメラからセル AABB までの最大描画距離(0=無制限)


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


			/**
			 * ベイク済み静的配置(セル単位カリング)
			 */
		public:
			/** ベイク結果を差し替える(ワーカーが作った不変データを move で受け取る) */
			inline void SetBakedData(BakedData&& baked) { baked_ = std::move(baked); }

			/**
			 * 現在の instancePoints_ をその場でベイクする。
			 * ベイク後は元の配置を解放するので、以後 instancePoints_ 経路には戻らない。
			 */
			void BakeStatic(const aq::math::Matrix4x4& entityWorld, const aq::math::AABB& meshLocalBounds,
			                const float cellSize);

			/** ベイク済みか(true ならベイク経路で描画される) */
			inline bool IsBaked() const { return !baked_.cells.empty(); }

			/** ベイク済みデータ */
			inline const BakedData& GetBaked() const { return baked_; }

			/** ベイク結果を破棄する */
			inline void ClearBaked() { baked_.instances.clear(); baked_.cells.clear(); }

			/** 最大描画距離(カメラ位置からセル AABB までの最短距離 [m]。0=無制限) */
			inline void  SetMaxDrawDistance(const float d) { maxDrawDistance_ = d; }
			inline float GetMaxDrawDistance() const        { return maxDrawDistance_; }


		public:
			// 永続フィールドなし(座標はコード/ImGui から動的に構築する)。
			template <typename V>
			void Reflect(V&) {}


			/**
			 * ベイク処理(純CPU計算・スレッド安全)
			 */
		public:
			/**
			 * InstancePoint 列を「セル分けしたベイク済み InstanceData 列」へ変換する。
			 * GPU リソースにも ECS にもシングルトンにも触れないため、ワーカースレッドから呼べる。
			 * @param points          変換元の配置(エンティティローカル)
			 * @param entityWorld     織り込むエンティティのワールド行列(以後このエンティティは動かせない)
			 * @param meshLocalBounds セル AABB にメッシュの大きさを含めるためのローカル AABB
			 * @param cellSize        XZ グリッドの1辺 [m]。0 以下なら全点を1セルにまとめる
			 * @return ベイク済みデータ(instances は cells の順に連続配置)
			 */
			static BakedData BuildBakedData(const std::vector<InstancePoint>& points,
			                                const aq::math::Matrix4x4& entityWorld,
			                                const aq::math::AABB& meshLocalBounds,
			                                const float cellSize);
		};
	}
}
