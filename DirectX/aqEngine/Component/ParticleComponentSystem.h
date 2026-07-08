#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "ECS/ECS.h"
#include "ECS/System.h"
#include "Math/Vector.h"
#include "Resource/ParticleSystemData.h"
#include "Rendering/RenderFrame.h"


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
			std::vector<aq::math::Vector3> initialSize;    // 軸別 (3D Start Size。一様時は xyz 同値)
			std::vector<aq::math::Vector4> initialColor;
			std::vector<float>             rotation;       // 現在の回転 (度, ビュー軸ロール)
			std::vector<aq::math::Vector3> size;           // 現在サイズ (initialSize * sizeOverLifetime)
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
		 * エミッタ 1 本ぶんの描画用 GPU リソース。
		 * 動的 VB (今フレームのビルボード頂点) と静的 IB (クアッドインデックス) を
		 * maxParticles ぶん確保して使い回す。
		 */
		struct ParticleEmitterRenderState
		{
			std::shared_ptr<graphics::IVertexBuffer> vb;
			std::shared_ptr<graphics::IIndexBuffer>  ib;
			uint32_t quadCapacity = 0;     // vb/ib が確保している粒子数
			uint32_t vertsPerParticle = 0; // Mesh 用: 1 粒子あたり頂点数 (billboard は 4 相当・未使用)
			uint32_t idxPerParticle   = 0; // Mesh 用: 1 粒子あたり index 数
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
			aq::math::Vector3                 lastWorldOrigin_;   // Local エミッタの描画基点 (Simulate で更新)

			// 描画用 (遅延生成)
			std::vector<ParticleEmitterRenderState> renderStates_;
			aq::res::RefShaderResource        particleVs_;
			aq::res::RefShaderResource        particlePs_;          // 手続き円 (テクスチャ無し)
			aq::res::RefShaderResource        particlePsTextured_;  // renderer.texture 指定時
			std::shared_ptr<graphics::ISamplerState> sampler_;      // s0 (linear/clamp)
			std::vector<aq::res::RefGPUResource>     textures_;     // エミッタごと (空パスなら nullptr)
			std::vector<aq::res::RefMeshResource>    meshes_;       // Mesh エミッタごと (それ以外は nullptr)

		public:
			ParticleEmitterComponent() = default;

			/** `.particle` アセットのパスを設定し、非同期ロードを開始する。 */
			void SetAsset(const char* path);
			const std::string& GetAssetPath() const { return assetPath_; }

			/** 再生/停止。停止中は既存粒子の更新も新規放出も行わない。 */
			void SetPlaying(bool v) { playing_ = v; }
			bool IsPlaying() const  { return playing_; }

			/** 全エミッタを頭から再生し直す (生存粒子を消し再生状態をリセット)。 */
			void Restart();

			bool IsReady() const { return state_ == State::Ready; }

			/**
			 * dt 秒ぶんシミュレーションを進める (ParticleSystem から毎フレーム)。
			 * worldOrigin は同居する HTC のワールド位置。ロード未完了なら何もしない。
			 */
			void Simulate(float dt, const aq::math::Vector3& worldOrigin);

			/**
			 * 生存粒子からカメラ向きビルボードを生成し、動的 VB を更新して
			 * ParticleRenderItem を out へ積む (RenderSystem::BuildRenderFrame から)。
			 * フリップブック UV / 距離ソート / StretchedBillboard をここで解決する。
			 * @param camRight   ワールド空間のカメラ右ベクトル
			 * @param camUp      ワールド空間のカメラ上ベクトル
			 * @param camForward ワールド空間のカメラ前方ベクトル (Stretched の横軸算出用)
			 * @param camPos     ワールド空間のカメラ位置 (距離ソート用)
			 */
			void FillParticleItems(std::vector<aq::rendering::ParticleRenderItem>& out,
			                       const aq::math::Vector3& camRight,
			                       const aq::math::Vector3& camUp,
			                       const aq::math::Vector3& camForward,
			                       const aq::math::Vector3& camPos);

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

			/** 描画シェーダ (Particle.fx) がロード完了しているか (デバッグ表示用)。 */
			bool ShadersReady() const
			{
				return particleVs_ && particleVs_->IsCompleted()
				    && particlePs_ && particlePs_->IsCompleted();
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
			/** Particle.fx の VS/PS (手続き円・テクスチャ) をロードし、使用可能なら true。 */
			bool EnsureShaders();
			/** renderer.texture のテクスチャと s0 サンプラを (未生成なら) 用意する。 */
			void EnsureTextures();
			/** Mesh レンダラーの renderer.mesh を (未生成なら) 非同期ロードする。 */
			void EnsureMeshes();
			/**
			 * バンドル内アセットのパスを解決する。
			 * "Assets/…" 始まりはコンテンツルート相対としてそのまま、それ以外は
			 * .particle ファイルのあるフォルダ相対 (エクスポータの Textures/…・Meshes/… バンドル) として解決する。
			 */
			std::string ResolveBundlePath(const std::string& path) const;
			/** rs を Mesh 描画用 (VB=vpp*maxParticles / IB=メッシュ index パターン) に用意する。 */
			void EnsureMeshBuffers(ParticleEmitterRenderState& rs, uint32_t vpp, uint32_t ipp,
			                       const std::vector<uint32_t>& meshIndices, uint32_t maxParticles);
			/** Mesh エミッタ 1 本を頂点展開して out へ積む (FillParticleItems から)。 */
			void FillMeshEmitter(size_t emitterIndex, ParticleEmitterRenderState& rs,
			                     const ParticleEmitterRuntime& rt, const aq::particle::EmitterData& e,
			                     std::vector<aq::rendering::ParticleRenderItem>& out,
			                     aq::res::RefGPUResource texRes, graphics::IShaderResourceView* srv,
			                     bool textured, bool additive);
			/** rs の VB/IB を quadCap ぶん (未確保/不足時のみ) 生成する。 */
			void EnsureBuffers(ParticleEmitterRenderState& rs, uint32_t quadCap);
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

#ifdef AQ_DEBUG_IMGUI
			const char* GetDebugGroup()    const override { return "Particle"; }
			const char* GetDebugTabLabel() const override { return "Particle"; }
			void        RenderContent()          override;
#endif
		};
	}
}
