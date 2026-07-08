#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "Math/Vector.h"


namespace aq
{
	namespace particle
	{
		/** カーブ / グラデーションを焼き込む LUT のサンプル数 (particleフォーマット仕様v1 §4.2/§4.4) */
		static constexpr uint32_t LUT_SAMPLE_COUNT = 64u;

		/** LUT オフセット未設定 (Constant / TwoConstants は LUT を持たない) */
		static constexpr uint32_t INVALID_LUT_OFFSET = 0xffffffffu;


		/**
		 * TwoConstants / TwoCurves の乱数 r を導出する項目 ID。
		 * 粒子ごとの 32bit シードと組み合わせ Hash(seed ^ 項目ID) で項目間の相関を断つ
		 * (particleフォーマット仕様v1 §5)。
		 */
		enum class RandomItem : uint32_t
		{
			Lifetime = 1,   // 0 はシード自身と衝突しやすいので 1 始まり
			Speed,
			Size,
			SizeY,          // 3D Start Size 用 (X は Size を共用)
			SizeZ,
			Rotation,       // Z ロール (3D Start Rotation の Z も共用)
			RotationX,      // 3D Start Rotation 用
			RotationY,
			Color,
			GravityModifier,
			RateOverTime,
			BurstCount,
			VelocityX,
			VelocityY,
			VelocityZ,
			SizeOverLifetime,
			SizeOverLifetimeY,   // Separate Axes 用 (X は SizeOverLifetime を共用)
			SizeOverLifetimeZ,
			RotationOverLifetime,
			Frame,
			StartFrame,
			ShapeDirection,   // shape の方向ランダム化用
			ShapePosition,    // shape の位置ランダム化用
		};




		/** カーブ LUT を線形補間サンプルする。t は [0,1] にクランプ。 */
		inline float SampleLut(const float* lut, const float t)
		{
			float clamped = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
			float pos     = clamped * static_cast<float>(LUT_SAMPLE_COUNT - 1u);
			uint32_t i0   = static_cast<uint32_t>(pos);
			if (i0 >= LUT_SAMPLE_COUNT - 1u) {
				return lut[LUT_SAMPLE_COUNT - 1u];
			}
			float frac = pos - static_cast<float>(i0);
			return lut[i0] + (lut[i0 + 1u] - lut[i0]) * frac;
		}




		/**
		 * スカラー値の分布 (Unity MinMaxCurve 互換・焼き込み済み)。
		 *
		 * Curve / TwoCurves は EmitterData::curveLutPool 内の LUT を lutMinOffset /
		 * lutMaxOffset で参照する。multiplier は焼き込み済みのため実行時乗算は不要
		 * (particleフォーマット仕様v1 §4.1/§4.2)。
		 */
		struct ScalarValue
		{
			enum class Mode : uint8_t { Constant, TwoConstants, Curve, TwoCurves };

			Mode     mode         = Mode::Constant;
			float    a            = 0.0f;                 // Constant値 / TwoConstants.min
			float    b            = 0.0f;                 // TwoConstants.max
			uint32_t lutMinOffset = INVALID_LUT_OFFSET;  // Curve / TwoCurves.min
			uint32_t lutMaxOffset = INVALID_LUT_OFFSET;  // TwoCurves.max


			/**
			 * 値を評価する。
			 * @param lutBase EmitterData::curveLutPool.data()。Constant/TwoConstants では未使用
			 * @param t       カーブ横軸 (項目により意味が異なる。仕様 §5 参照)
			 * @param r       粒子ごとの乱数 [0,1] (TwoConstants/TwoCurves の補間係数)
			 */
			inline float Evaluate(const float* lutBase, const float t, const float r) const
			{
				switch (mode) {
				case Mode::Constant:     return a;
				case Mode::TwoConstants: return a + (b - a) * r;
				case Mode::Curve:        return SampleLut(lutBase + lutMinOffset, t);
				case Mode::TwoCurves:
				{
					float lo = SampleLut(lutBase + lutMinOffset, t);
					float hi = SampleLut(lutBase + lutMaxOffset, t);
					return lo + (hi - lo) * r;
				}
				}
				return a;
			}
		};


		/**
		 * 色の分布 (Unity MinMaxGradient のサブセット・startColor 用)。
		 * v1 は Constant / TwoConstants のみ (仕様 §4.3)。
		 */
		struct ColorValue
		{
			enum class Mode : uint8_t { Constant, TwoConstants };

			Mode          mode = Mode::Constant;
			math::Vector4 a    = math::Vector4(1.0f, 1.0f, 1.0f, 1.0f);   // Constant / min
			math::Vector4 b    = math::Vector4(1.0f, 1.0f, 1.0f, 1.0f);   // max


			inline math::Vector4 Evaluate(const float r) const
			{
				if (mode == Mode::Constant) {
					return a;
				}
				return math::Vector4(
					a.x + (b.x - a.x) * r,
					a.y + (b.y - a.y) * r,
					a.z + (b.z - a.z) * r,
					a.w + (b.w - a.w) * r);
			}
		};




		/** シミュレーション空間 (仕様 §7.1/§7.5) */
		enum class SimulationSpace : uint8_t { Local, World };

		/** 生成形状 (仕様 §7.4) */
		enum class ShapeType : uint8_t { Cone, Sphere, Box, Circle };

		/** 描画方式 (仕様 §7.10) */
		enum class RendererType : uint8_t { Billboard, StretchedBillboard, Mesh };

		/** ブレンド (仕様 §7.10) */
		enum class BlendMode : uint8_t { Alpha, Additive };

		/** ソート (仕様 §7.10) */
		enum class SortMode : uint8_t { None, ByDistance };




		/**
		 * 生成時の初期値 (仕様 §7.2)
		 */
		struct InitialModule
		{
			ScalarValue lifetime;   // 既定 5.0
			ScalarValue speed;      // 既定 5.0
			ScalarValue size;       // 既定 1.0 (一様)
			ScalarValue rotation;   // 既定 0.0 (度)
			ColorValue  color;      // 既定 白

			/** 3D Start Size (size3D 指定時のみ有効。ビーム板/円筒の非一様スケール用) */
			bool        size3D = false;
			ScalarValue sizeX;
			ScalarValue sizeY;
			ScalarValue sizeZ;

			/** 3D Start Rotation (rotation3D 指定時のみ有効。度。メッシュ粒子の向き付け用) */
			bool        rotation3D = false;
			ScalarValue rotationX;
			ScalarValue rotationY;
			ScalarValue rotationZ;
		};


		/**
		 * バースト定義 (仕様 §7.3)
		 */
		struct EmissionBurst
		{
			float       time        = 0.0f;
			ScalarValue count;                 // 既定 0
			int32_t     cycles      = 1;       // 0 = 無限リピート
			float       interval    = 0.01f;
			float       probability = 1.0f;
		};

		/**
		 * 放出 (仕様 §7.3)
		 */
		struct EmissionModule
		{
			bool                       enabled = true;
			ScalarValue                rateOverTime;   // 既定 10.0
			std::vector<EmissionBurst> bursts;
		};


		/**
		 * 生成形状 (仕様 §7.4)
		 */
		struct ShapeModule
		{
			bool          enabled         = true;
			ShapeType     type            = ShapeType::Cone;
			float         angle           = 25.0f;               // Cone 開き角 (度)
			float         radius          = 1.0f;
			float         radiusThickness = 1.0f;                // 0 = 表面のみ, 1 = 全体
			math::Vector3 boxSize         = math::Vector3(1.0f, 1.0f, 1.0f);
			math::Vector3 position        = math::Vector3(0.0f, 0.0f, 0.0f);
			math::Vector3 rotationEuler   = math::Vector3(0.0f, 0.0f, 0.0f);
		};


		/**
		 * 速度の時間変化 (仕様 §7.5)
		 */
		struct VelocityOverLifetimeModule
		{
			bool            enabled = false;
			ScalarValue     linearX;
			ScalarValue     linearY;
			ScalarValue     linearZ;
			SimulationSpace space   = SimulationSpace::Local;
		};


		/**
		 * 色の時間変化 (仕様 §7.6)。initial.color に乗算する。
		 * gradientLut は 64 サンプルの RGBA (焼き込み済み)。
		 */
		struct ColorOverLifetimeModule
		{
			bool                       enabled = false;
			std::vector<math::Vector4> gradientLut;   // size == LUT_SAMPLE_COUNT (enabled 時)

			inline math::Vector4 Evaluate(const float u) const
			{
				float clamped = u < 0.0f ? 0.0f : (u > 1.0f ? 1.0f : u);
				float pos     = clamped * static_cast<float>(LUT_SAMPLE_COUNT - 1u);
				uint32_t i0   = static_cast<uint32_t>(pos);
				if (i0 >= LUT_SAMPLE_COUNT - 1u) {
					return gradientLut[LUT_SAMPLE_COUNT - 1u];
				}
				float frac = pos - static_cast<float>(i0);
				const math::Vector4& lo = gradientLut[i0];
				const math::Vector4& hi = gradientLut[i0 + 1u];
				return math::Vector4(
					lo.x + (hi.x - lo.x) * frac,
					lo.y + (hi.y - lo.y) * frac,
					lo.z + (hi.z - lo.z) * frac,
					lo.w + (hi.w - lo.w) * frac);
			}
		};


		/**
		 * サイズの時間変化 (仕様 §7.7)。initial.size に乗算する倍率。
		 */
		struct SizeOverLifetimeModule
		{
			bool        enabled = false;
			ScalarValue size;

			/** Separate Axes (size3D 指定時のみ有効。軸別の倍率カーブ) */
			bool        separateAxes = false;
			ScalarValue sizeX;
			ScalarValue sizeY;
			ScalarValue sizeZ;
		};


		/**
		 * 回転の時間変化 (仕様 §7.8)。角速度 (度/秒)。
		 */
		struct RotationOverLifetimeModule
		{
			bool        enabled = false;
			ScalarValue angularVelocity;
		};


		/**
		 * フリップブック (仕様 §7.9)
		 */
		struct TextureSheetAnimationModule
		{
			bool        enabled = false;
			int32_t     tilesX  = 1;
			int32_t     tilesY  = 1;
			ScalarValue frameOverTime;   // 0〜1 正規化
			ScalarValue startFrame;      // フレーム単位
			int32_t     cycles  = 1;
		};


		/**
		 * 描画設定 (仕様 §7.10)
		 */
		struct RendererModule
		{
			RendererType type        = RendererType::Billboard;
			std::string  texture;                                // 解決規約は仕様 §7.10.1
			std::string  mesh;
			BlendMode    blendMode   = BlendMode::Alpha;
			SortMode     sortMode    = SortMode::None;
			float        lengthScale = 2.0f;
			float        speedScale  = 0.0f;

			/** マテリアルの Tiling/Offset (エクスポータが左上原点へ変換済み)。uv' = uvOffset + uv * uvScale */
			math::Vector2 uvScale  = math::Vector2(1.0f, 1.0f);
			math::Vector2 uvOffset = math::Vector2(0.0f, 0.0f);
		};




		/**
		 * エミッタ 1 つ分の焼き込み済みデータ (仕様 §7)。
		 * ScalarValue の Curve/TwoCurves は curveLutPool 内の LUT を参照する。
		 */
		struct EmitterData
		{
			/** 基本 (仕様 §7.1) */
			std::string     name              = "emitter";
			math::Vector3   localPosition     = math::Vector3(0.0f, 0.0f, 0.0f);
			math::Vector3   localRotationEuler = math::Vector3(0.0f, 0.0f, 0.0f);
			float           duration          = 5.0f;
			bool            looping           = true;
			float           startDelay        = 0.0f;
			int32_t         maxParticles      = 1000;
			SimulationSpace simulationSpace   = SimulationSpace::Local;
			ScalarValue     gravityModifier;   // 既定 0

			/** モジュール群 */
			InitialModule               initial;
			EmissionModule              emission;
			ShapeModule                 shape;
			VelocityOverLifetimeModule  velocityOverLifetime;
			ColorOverLifetimeModule     colorOverLifetime;
			SizeOverLifetimeModule      sizeOverLifetime;
			RotationOverLifetimeModule  rotationOverLifetime;
			TextureSheetAnimationModule textureSheetAnimation;
			RendererModule              renderer;

			/** このエミッタの全 ScalarValue が参照するカーブ LUT プール (連結配置) */
			std::vector<float> curveLutPool;
		};
	}
}
