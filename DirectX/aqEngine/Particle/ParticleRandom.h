#pragma once
#include <cstdint>
#include "Particle/ParticleTypes.h"


namespace aq
{
	namespace particle
	{
		/**
		 * 32bit 整数ハッシュ (MurmurHash3 fmix32)。
		 * 粒子シードと項目 ID を混ぜて相関のない乱数列を得るのに使う。
		 */
		inline uint32_t Hash(uint32_t x)
		{
			x ^= x >> 16;
			x *= 0x7feb352du;
			x ^= x >> 15;
			x *= 0x846ca68bu;
			x ^= x >> 16;
			return x;
		}


		/** seed と任意ソルトから [0,1) の一様乱数を導出する。 */
		inline float RandomUnit(uint32_t seed, uint32_t salt)
		{
			return static_cast<float>(Hash(seed ^ salt)) * (1.0f / 4294967296.0f);
		}


		/**
		 * seed と項目 ID から [0,1) の一様乱数を導出する。
		 * TwoConstants / TwoCurves の補間係数 r に使う。項目間の相関を断つ。
		 */
		inline float RandomUnit(uint32_t seed, RandomItem item)
		{
			return RandomUnit(seed, static_cast<uint32_t>(item));
		}
	}
}
