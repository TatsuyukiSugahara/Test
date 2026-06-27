/**
 * カリング用バウンディング型 (AABB) と視錐台 (Frustum)
 *
 * AABB     : 軸並行バウンディングボックス。中心 + 半サイズ (extent) で保持する。
 * Frustum  : ビュープロジェクション行列から抽出した 6 平面。AABB との内外判定を行う。
 *
 * DirectX (行ベクトル, クリップ z [0,1]) の規約に合わせて実装している。
 */
#pragma once
#include <cfloat>
#include <cmath>
#include "Vector.h"
#include "Matrix.h"

namespace aq
{
	namespace math
	{
		/** 軸並行バウンディングボックス (中心 + 半サイズ) */
		struct AABB
		{
			Vector3 center;
			Vector3 extent;  // 半サイズ (各軸の中心からの距離)

			AABB() : center(0.0f, 0.0f, 0.0f), extent(0.0f, 0.0f, 0.0f) {}
			AABB(const Vector3& c, const Vector3& e) : center(c), extent(e) {}

			/** 最小・最大点から AABB を構築 */
			static AABB FromMinMax(const Vector3& min, const Vector3& max)
			{
				Vector3 c(
					(min.x + max.x) * 0.5f,
					(min.y + max.y) * 0.5f,
					(min.z + max.z) * 0.5f);
				Vector3 e(
					(max.x - min.x) * 0.5f,
					(max.y - min.y) * 0.5f,
					(max.z - min.z) * 0.5f);
				return AABB(c, e);
			}

			/** 空 (まだ点を含まない) 判定。FromMinMax で潰れた状態を表す */
			bool IsEmpty() const
			{
				return extent.x < 0.0f || extent.y < 0.0f || extent.z < 0.0f;
			}

			/**
			 * ワールド行列 M を適用したワールド空間 AABB を返す。
			 * 回転を吸収するため、各軸 extent を |M| の上 3x3 で射影し直す。
			 */
			AABB Transformed(const Matrix4x4& m) const
			{
				// 中心をワールドへ
				DirectX::XMVECTOR c = DirectX::XMVector3TransformCoord(center, m);
				Vector3 worldCenter;
				DirectX::XMStoreFloat3(&worldCenter.vector, c);

				// extent は上 3x3 の絶対値で射影
				Vector3 worldExtent(
					std::fabs(m._11) * extent.x + std::fabs(m._21) * extent.y + std::fabs(m._31) * extent.z,
					std::fabs(m._12) * extent.x + std::fabs(m._22) * extent.y + std::fabs(m._32) * extent.z,
					std::fabs(m._13) * extent.x + std::fabs(m._23) * extent.y + std::fabs(m._33) * extent.z);
				return AABB(worldCenter, worldExtent);
			}
		};


		/** ロード時に頂点列から AABB を求めるためのビルダ */
		class AABBBuilder
		{
		private:
			Vector3 min_;
			Vector3 max_;
			bool    hasPoint_ = false;

		public:
			void Add(const Vector3& p)
			{
				if (!hasPoint_)
				{
					min_ = p;
					max_ = p;
					hasPoint_ = true;
					return;
				}
				min_.Set(std::fmin(min_.x, p.x), std::fmin(min_.y, p.y), std::fmin(min_.z, p.z));
				max_.Set(std::fmax(max_.x, p.x), std::fmax(max_.y, p.y), std::fmax(max_.z, p.z));
			}

			bool HasPoint() const { return hasPoint_; }

			AABB Build() const
			{
				if (!hasPoint_) return AABB();
				return AABB::FromMinMax(min_, max_);
			}
		};


		/** 平面 a*x + b*y + c*z + d = 0。法線 (a,b,c) 側が内側 (>= 0)。 */
		struct Plane
		{
			float a = 0.0f;
			float b = 0.0f;
			float c = 0.0f;
			float d = 0.0f;

			void Normalize()
			{
				float len = std::sqrt(a * a + b * b + c * c);
				if (len > 0.0f)
				{
					float inv = 1.0f / len;
					a *= inv; b *= inv; c *= inv; d *= inv;
				}
			}
		};


		/** ビュープロジェクション行列から抽出した 6 平面の視錐台 */
		class Frustum
		{
		public:
			enum { LEFT, RIGHT, BOTTOM, TOP, NEAR_, FAR_, PLANE_COUNT };

		private:
			Plane planes_[PLANE_COUNT];

		public:
			/**
			 * ビュープロジェクション行列 (view * projection) から 6 平面を抽出する。
			 * DirectX (行ベクトル, p' = p * M, クリップ z [0,1]) の Gribb-Hartmann 法。
			 */
			void FromViewProjection(const Matrix4x4& m)
			{
				// 列ベクトル col[k][i] = m[i][k]
				// clip.x = dot(p, col0), clip.y = col1, clip.z = col2, clip.w = col3
				// Left   : clip.w + clip.x >= 0
				planes_[LEFT]   = { m._14 + m._11, m._24 + m._21, m._34 + m._31, m._44 + m._41 };
				planes_[RIGHT]  = { m._14 - m._11, m._24 - m._21, m._34 - m._31, m._44 - m._41 };
				planes_[BOTTOM] = { m._14 + m._12, m._24 + m._22, m._34 + m._32, m._44 + m._42 };
				planes_[TOP]    = { m._14 - m._12, m._24 - m._22, m._34 - m._32, m._44 - m._42 };
				// クリップ z [0,1] なので Near は col2、Far は col3 - col2
				planes_[NEAR_]  = { m._13,         m._23,         m._33,         m._43 };
				planes_[FAR_]   = { m._14 - m._13, m._24 - m._23, m._34 - m._33, m._44 - m._43 };

				for (int i = 0; i < PLANE_COUNT; ++i)
					planes_[i].Normalize();
			}

			/** 6 平面を (a,b,c,d) の Vector4 として取り出す (GPU CB へ渡す用)。 */
			void GetPlanes(Vector4 out[PLANE_COUNT]) const
			{
				for (int i = 0; i < PLANE_COUNT; ++i)
					out[i] = Vector4(planes_[i].a, planes_[i].b, planes_[i].c, planes_[i].d);
			}

			/**
			 * ワールド空間 AABB が視錐台と交差 (または内包) するか。
			 * 1 つでも完全に外側の平面があれば false (カリング対象)。
			 */
			bool Intersects(const AABB& box) const
			{
				for (int i = 0; i < PLANE_COUNT; ++i)
				{
					const Plane& p = planes_[i];
					// 中心の符号付き距離
					float s = p.a * box.center.x + p.b * box.center.y + p.c * box.center.z + p.d;
					// AABB の平面法線方向への射影半径
					float r = std::fabs(p.a) * box.extent.x
					        + std::fabs(p.b) * box.extent.y
					        + std::fabs(p.c) * box.extent.z;
					// 最も内側の点でも平面の外 → 完全に外側
					if (s + r < 0.0f) return false;
				}
				return true;
			}
		};
	}
}
