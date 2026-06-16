#pragma once
#include <cstdint>
#include "Math/Vector.h"


namespace engine
{
	namespace physics
	{
		constexpr uint32_t kCollisionAttrAll  = 0xFFFFFFFF;
		constexpr uint32_t kCollisionAttrNone = 0x00000000;


		/**
		 * Raycast のヒット情報。バックエンド非依存。
		 */
		struct RaycastHit
		{
			math::Vector3 point;
			math::Vector3 normal;
			float         distance = 0.0f;
			float         fraction = 0.0f; // 0=始点, 1=終点
			void*         userPtr  = nullptr;
		};


		/**
		 * ConvexSweep のヒット情報。バックエンド非依存。
		 */
		struct SweepHit
		{
			math::Vector3 point;
			math::Vector3 normal;
			float         fraction = 0.0f;
			void*         userPtr  = nullptr;
		};
	}
}
