#pragma once
#include <cstdint>
#include "Math/Vector.h"

namespace aq
{
	namespace ocean
	{
		// ============================================================
		// OceanParams — ゲームコード側が設定する海パラメータ
		// ============================================================
		struct OceanParams
		{
			// メッシュ
			float    size       = 100.0f;
			uint32_t resolution = 128;     // 分割数 (resolution x resolution グリッド)

			// 法線マップ (オプション。両方 null でも動く)
			const char* normalMapPath1 = nullptr;
			const char* normalMapPath2 = nullptr;

			// 法線マップ UV スクロール
			float normalScale1  = 2.0f;
			float normalDirX1   = 0.7f,  normalDirZ1  =  0.5f;
			float normalSpeed1  = 0.04f;
			float normalScale2  = 1.3f;
			float normalDirX2   = -0.4f, normalDirZ2  =  0.8f;
			float normalSpeed2  = 0.03f;

			// 海の色
			math::Vector3 deepColor    = { 0.02f, 0.07f, 0.15f };
			math::Vector3 shallowColor = { 0.08f, 0.25f, 0.40f };

			// Fresnel パラメータ
			float fresnelBias  = 0.04f;   // 最低反射率
			float fresnelScale = 1.0f;
			float fresnelPower = 4.0f;    // べき乗: 大きいほどシャープに切り替わる

			// 太陽ハイライト
			float sunShininess = 256.0f;  // Blinn-Phong の光沢度
			float sunIntensity = 2.5f;    // ハイライト強度

			// 空の反射色 (映り込みオブジェクトなしの場合はこの色でFresnel補間)
			math::Vector3 skyColor = { 0.55f, 0.75f, 0.95f };

			// Gerstner 波 水平変位係数
			// 0=純粋な正弦波 (滑らか), 1=最大急峻 (尖った峰)
			// kA の総和が 1 未満になるよう振幅・波長に合わせて調整する
			float waveQ = 0.0f;

			// Gerstner 波 (最大4波を重畳)
			struct Wave
			{
				float dirX      =  1.0f;   // 伝播方向 X (正規化不要 — シェーダー内で使用)
				float dirZ      =  0.0f;   // 伝播方向 Z
				float amplitude =  0.4f;   // 振幅 (m)
				float wavelength = 15.0f;  // 波長 (m)
				float speed     =  1.5f;   // 位相速度 (m/s)
			};
			Wave waves[4] = {
				{  1.0f,  0.3f,  2.0f, 18.0f, 1.2f },  // 主うねり
				{  0.5f,  1.0f,  1.0f, 12.0f, 1.8f },  // 斜め
				{ -0.7f,  0.6f,  0.5f,  7.0f, 2.5f },  // 斜め逆
				{  0.4f, -0.9f,  0.2f,  4.0f, 3.5f },  // 細かいチョップ
			};
		};


		// ============================================================
		// OceanCBData — HLSL OceanCB (b5) と完全一致 (192 bytes)
		// OceanCB.h を修正した場合はこちらも合わせて修正すること
		// ============================================================
		struct alignas(16) OceanCBData
		{
			// row 0: time + 水平変位係数
			float time; float steepness; float _p0[2];

			// row 1: 法線マップ1 UV パラメータ
			float normalScale1, normalDirX1, normalDirZ1, normalSpeed1;

			// row 2: 法線マップ2 UV パラメータ
			float normalScale2, normalDirX2, normalDirZ2, normalSpeed2;

			// row 3: 深い海の色
			float deepR, deepG, deepB, _p1;

			// row 4: 浅い海の色
			float shallR, shallG, shallB, _p2;

			// row 5: Fresnel + 太陽の光沢度
			float fresnelBias, fresnelScale, fresnelPower, sunShininess;

			// row 6: 太陽の強度 + 空の色
			float sunIntensity, skyR, skyG, skyB;

			// row 7-10: 波パラメータ (dirX, dirZ, amplitude, wavelength)
			struct WaveGPU { float dirX, dirZ, amplitude, wavelength; } waves[4];

			// row 11: 波の位相速度
			float waveSpeeds[4];
		};
		static_assert(sizeof(OceanCBData) == 192, "OceanCBData のサイズが HLSL OceanCB と一致しません");
	}
}
