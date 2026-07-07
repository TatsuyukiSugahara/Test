#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "ECS/ECS.h"
#include "ECS/System.h"
#include "Math/Vector.h"
#include "Resource/ParticleSystemData.h"


namespace aq
{
	namespace ecs
	{
		/**
		 * エミッタ 1 本ぶんの実行時状態 (SoA プール + 再生状態)。
		 *
		 * 焼き込み済み定義 (aq::particle::EmitterData) は asset 側にあり、こちらは
		 * 生存粒子の可変データだけを持つ。描画側 (ParticleSystem / RenderSystem) が
		 * 読み取ってビルボードを生成する。
		 */
		struct ParticleEmitterRuntime
		{
			// --- SoA 粒子プール (容量 = maxParticles, 有効なのは先頭 aliveCount 個) ---
			std::vector<aq::math::Vector3> position;      // Local空間 or World空間 (simulationSpace)
			std::vector<aq::math::Vector3> velocity;
			std::vector<float>             age;
			std::vector<float>             lifetime;
			std::vector<uint32_t>          seed;
			std::vector<float>             initialSize;
			std::vector<aq::math::Vector4> initialColor;
			std::vector<float>             rotation;       // 現在の回転 (度, ビュー軸ロール)
			std::vector<float>             size;           // 現在サイズ (initialSize * sizeOverLifetime)
			std::vector<aq::math::Vector4> color;          // 現在カラー (initialColor * colorOverLifetime)

			uint32_t aliveCount = 0;

			// --- 再生状態 ---
			float    playbackTime   = 0.0f;   // startDelay 込みの経過秒
			float    emitAccumulator = 0.0f;  // rateOverTime の端数キャリー
			float    prevLoopTime   = 0.0f;   // バースト発火のループ折り返し検出用
			uint32_t spawnCounter   = 0;      // 粒子シード生成カウンタ
			uint32_t seedBase       = 0;      // このエミッタのシード基点
			std::vector<uint8_t> burstFired;  // バーストごとの発火済みフラグ (今ループ内)

			void Init(const aq::particle::EmitterData& data, uint32_t emitterIndex);
		};


		/**
		 * パーティクルエミッタ コンポーネント。
		 *
		 * `.particle` アセット (aq::res::ParticleSystemData) を参照し、その全エミッタを
		 * CPU シミュレーションする。原点は同居する HierarchicalTransformComponent の
		 * ワールド変換から得る (ParticleSystem 経由)。
		 */
		class ParticleEmitterComponent : public aq::ecs::IComponent
		{
			ecsComponent(aq::ecs::ParticleEmitterComponent);

		private:
			enum class State : uint8_t
			{
				Empty,       // アセット未指定
				Loading,     // アセットロード中
				Ready,       // ランタイム構築済み
			};

			State                             state_ = State::Empty;
			std::string                       assetPath_;
			aq::res::RefParticleSystemResource asset_;
			std::vector<ParticleEmitterRuntime> runtimes_;
			bool                              playing_ = true;

		public:
			ParticleEmitterComponent() = default;

			/** `.particle` アセットのパスを設定し、非同期ロードを開始する。 */
			void SetAsset(const char* path);
			const std::string& GetAssetPath() const { return assetPath_; }

			/** 再生/停止。停止中は既存粒子の更新も新規放出も行わない。 */
			void SetPlaying(bool v) { playing_ = v; }
			bool IsPlaying() const  { return playing_; }

			bool IsReady() const { return state_ == State::Ready; }

			/**
			 * dt 秒ぶんシミュレーションを進める (ParticleSystem から毎フレーム)。
			 * worldOrigin は同居する HTC のワールド位置。ロード未完了なら何もしない。
			 */
			void Simulate(float dt, const aq::math::Vector3& worldOrigin);

			/** 描画側が参照する読み取り用アクセサ。 */
			const aq::res::ParticleSystemData*        GetAsset()    const { return asset_.get(); }
			const std::vector<ParticleEmitterRuntime>& GetRuntimes() const { return runtimes_; }

			/** 全エミッタの生存粒子総数 (デバッグ/計測用)。 */
			uint32_t GetAliveCount() const
			{
				uint32_t n = 0;
				for (const ParticleEmitterRuntime& rt : runtimes_) n += rt.aliveCount;
				return n;
			}

#ifdef AQ_DEBUG_IMGUI
			template <typename V>
			void Inspect(V& visitor)
			{
				std::string newPath = assetPath_;
				if (visitor.FieldPath("particle", newPath, "Particle Asset"))
					SetAsset(newPath.empty() ? nullptr : newPath.c_str());
				visitor.ReadOnly("state", state_ == State::Ready ? "Ready" : (state_ == State::Loading ? "Loading" : "Empty"));
			}
#endif

			// 永続フィールド (JSON 保存/読込)。ロード副作用は OnDeserialized へ退避。
			template <typename V>
			void Reflect(V& visitor)
			{
				visitor.FieldPath("particle", assetPath_, "Particle Asset");
			}

			void OnDeserialized()
			{
				if (!assetPath_.empty())
					SetAsset(assetPath_.c_str());
			}

		private:
			/** asset ロード完了後に runtimes_ を構築する。 */
			void EnsureRuntimes();
		};


		/**
		 * パーティクル更新システム。
		 *
		 * ParticleEmitterComponent を持つ全エンティティを走査し、同居する
		 * HierarchicalTransformComponent のワールド位置を原点として CPU
		 * シミュレーションを 1 フレームぶん進める。
		 * ワールド変換確定後に走らせるため HierarcicalTransformSystem に依存させること。
		 * 描画データ生成は RenderSystem::BuildRenderFrame 側が runtimes を読んで行う。
		 */
		class ParticleSystem : public aq::ecs::SystemBase
		{
		public:
			void Update() override;
		};
	}
}
