/**
 * Mathの汎用処理群
 */
#pragma once

namespace aq
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

		/** [min, max] へのクランプ */
		template <typename T>
		static inline T Clamp(const T& value, const T& min, const T& max)
		{
			return value < min ? min : (value > max ? max : value);
		}

		/** [0, 1] へのクランプ */
		static inline float Clamp01(const float value)
		{
			return Clamp(value, 0.0f, 1.0f);
		}

		/**
		 * 線形補間: a + (b - a) * t。
		 * float のほか、operator+ / operator- / operator*(float) を持つ型
		 * (Vector2 / Vector3 / Vector4 等) でそのまま使える。t はクランプしない。
		 */
		template <typename T>
		static inline T Lerp(const T& a, const T& b, const float t)
		{
			return a + (b - a) * t;
		}

		/** 逆線形補間: value が [a, b] のどこにあるかを返す (クランプなし。a == b のときは 0) */
		static inline float InverseLerp(const float a, const float b, const float value)
		{
			const float range = b - a;
			if (range > -1e-6f && range < 1e-6f) { return 0.0f; }
			return (value - a) / range;
		}
	}
}