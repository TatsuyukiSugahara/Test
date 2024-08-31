/**
 * Math‚Ì”Ä—pˆ—ŒQ
 */
#pragma once

namespace engine
{
	namespace math
	{
		static float PIE = 3.141592654f;
		static float PIE2 = PIE * 2;
		static float PIE_HALF = PIE / 2;

		static inline float DegToRadian(const float degree)
		{
			return degree * (PIE / 180.0f);
		}
		static inline float RadToDegree(const float radian)
		{
			return radian * (180.0f / PIE);
		}
	}
}