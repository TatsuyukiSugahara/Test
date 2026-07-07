#include "aq.h"
#include "ParticleComponentSystem.h"
#include "Particle/ParticleRandom.h"
#include "Resource/Resource.h"
#include "Component/HierarchicalTransformComponent.h"
#include "Graphics/GraphicsDevice.h"
#include "Graphics/IShader.h"
#include <algorithm>
#include <cmath>
#ifdef AQ_DEBUG_IMGUI
#include <imgui/imgui.h>
#endif


namespace aq
{
	namespace ecs
	{
		// 焼き込み済みデータ型 (EmitterData / ScalarValue / 各 enum) は
		// aq::particle に定義されている。この翻訳単位に限って取り込む。
		using namespace aq::particle;

		namespace
		{
			using aq::math::Vector3;
			using aq::math::Vector4;

			constexpr float DEG2RAD = 3.14159265358979f / 180.0f;
			constexpr float TWO_PI  = 6.28318530717959f;
			constexpr float GRAVITY = 9.81f;


			/** エミッタ正規化時間 emitterNormT を求める。looping は折り返し、非ループはクランプ。 */
			float NormT(const EmitterData& e, float playbackTime)
			{
				float t   = playbackTime - e.startDelay;
				if (t < 0.0f) t = 0.0f;
				float dur = e.duration > 0.0f ? e.duration : 1.0f;
				if (e.looping)      t = std::fmod(t, dur);
				else if (t > dur)   t = dur;
				return t / dur;
			}


			/** shape から生成位置 (エミッタローカル) と初期方向 (単位) を求める。 */
			void SampleShape(const ShapeModule& shape, uint32_t seed, Vector3& outPos, Vector3& outDir)
			{
				if (!shape.enabled) {
					outPos = shape.position;
					outDir = Vector3(0.0f, 1.0f, 0.0f);
					return;
				}

				const float u1 = RandomUnit(seed, 0xA001u);
				const float u2 = RandomUnit(seed, 0xA002u);
				const float u3 = RandomUnit(seed, 0xA003u);

				switch (shape.type) {
				case ShapeType::Sphere:
				{
					// 単位球上の一様方向 + 半径 (radiusThickness で殻厚を近似)
					const float z = u1 * 2.0f - 1.0f;
					const float a = u2 * TWO_PI;
					const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
					outDir = Vector3(r * std::cos(a), z, r * std::sin(a));
					const float inner = 1.0f - std::max(0.0f, std::min(1.0f, shape.radiusThickness));
					const float rr = shape.radius * (inner + (1.0f - inner) * std::cbrt(u3));
					outPos = outDir * rr;
					break;
				}
				case ShapeType::Box:
				{
					outPos = Vector3((u1 - 0.5f) * shape.boxSize.x,
					                 (u2 - 0.5f) * shape.boxSize.y,
					                 (u3 - 0.5f) * shape.boxSize.z);
					outDir = Vector3(0.0f, 1.0f, 0.0f);
					break;
				}
				case ShapeType::Circle:
				{
					const float a  = u1 * TWO_PI;
					const float rr = std::sqrt(u2) * shape.radius;
					outPos = Vector3(std::cos(a) * rr, 0.0f, std::sin(a) * rr);
					outDir = Vector3(std::cos(a), 0.0f, std::sin(a));
					if (outDir.IsZero()) outDir = Vector3(0.0f, 1.0f, 0.0f);
					break;
				}
				case ShapeType::Cone:
				default:
				{
					// 底面ディスクから放出、+Y を軸に angle だけ外向きへ傾ける
					const float a  = u1 * TWO_PI;
					const float rr = std::sqrt(u2) * shape.radius;
					outPos = Vector3(std::cos(a) * rr, 0.0f, std::sin(a) * rr);
					Vector3 radial(std::cos(a), 0.0f, std::sin(a));
					const float ang = shape.angle * DEG2RAD;
					const float s = std::sin(ang);
					outDir = Vector3(radial.x * s, std::cos(ang), radial.z * s);
					outDir.Normalize();
					break;
				}
				}

				outPos = outPos + shape.position;
			}


			/** 粒子 1 個を生成してプール末尾へ積む。容量超過なら何もしない。 */
			void Spawn(ParticleEmitterRuntime& rt, const EmitterData& e,
			           float normT, const Vector3& origin, bool world)
			{
				if (rt.aliveCount >= rt.position.size()) return;

				const uint32_t s = Hash(rt.seedBase + rt.spawnCounter * 2654435761u);
				++rt.spawnCounter;
				const float* lut = e.curveLutPool.empty() ? nullptr : e.curveLutPool.data();

				const uint32_t i = rt.aliveCount++;
				rt.seed[i] = s;

				float life = e.initial.lifetime.Evaluate(lut, normT, RandomUnit(s, RandomItem::Lifetime));
				if (life <= 0.0f) life = 0.0001f;
				rt.lifetime[i] = life;
				rt.age[i]      = 0.0f;

				const float speed = e.initial.speed.Evaluate(lut, normT, RandomUnit(s, RandomItem::Speed));
				const float sz    = e.initial.size.Evaluate(lut, normT, RandomUnit(s, RandomItem::Size));
				const float rot   = e.initial.rotation.Evaluate(lut, normT, RandomUnit(s, RandomItem::Rotation));
				const Vector4 col = e.initial.color.Evaluate(RandomUnit(s, RandomItem::Color));

				rt.initialSize[i]  = sz;
				rt.initialColor[i] = col;
				rt.rotation[i]     = rot;
				rt.size[i]         = sz;
				rt.color[i]        = col;

				Vector3 lpos, ldir;
				SampleShape(e.shape, s, lpos, ldir);
				rt.velocity[i] = ldir * speed;
				rt.position[i] = world ? (origin + lpos) : lpos;
			}


			/** swap-remove 用に粒子 src を dst へ移す。 */
			inline void MoveParticle(ParticleEmitterRuntime& rt, uint32_t dst, uint32_t src)
			{
				rt.position[dst]     = rt.position[src];
				rt.velocity[dst]     = rt.velocity[src];
				rt.age[dst]          = rt.age[src];
				rt.lifetime[dst]     = rt.lifetime[src];
				rt.seed[dst]         = rt.seed[src];
				rt.initialSize[dst]  = rt.initialSize[src];
				rt.initialColor[dst] = rt.initialColor[src];
				rt.rotation[dst]     = rt.rotation[src];
				rt.size[dst]         = rt.size[src];
				rt.color[dst]        = rt.color[src];
			}


			/** エミッタ 1 本を dt 秒進める。emission → spawn → integrate の順。 */
			void StepEmitter(ParticleEmitterRuntime& rt, const EmitterData& e,
			                 float dt, const Vector3& origin, bool playing)
			{
				const float* lut  = e.curveLutPool.empty() ? nullptr : e.curveLutPool.data();
				const bool   world = (e.simulationSpace == SimulationSpace::World);

				if (playing) {
					rt.playbackTime += dt;
					const float normT     = NormT(e, rt.playbackTime);
					const float localTime = rt.playbackTime - e.startDelay;
					const bool  active    = e.looping || (localTime <= e.duration);

					// 連続放出 (rateOverTime)
					if (e.emission.enabled && active && localTime >= 0.0f) {
						const float rate = e.emission.rateOverTime.Evaluate(lut, normT, 0.5f);
						if (rate > 0.0f) {
							rt.emitAccumulator += rate * dt;
							while (rt.emitAccumulator >= 1.0f) {
								rt.emitAccumulator -= 1.0f;
								Spawn(rt, e, normT, origin, world);
							}
						}
					}

					// バースト (MVP: ループごとに time 到達で 1 回発火)
					if (active && localTime >= 0.0f && !e.emission.bursts.empty()) {
						const float dur   = e.duration > 0.0f ? e.duration : 1.0f;
						const float loopT = e.looping ? std::fmod(localTime, dur) : localTime;
						if (e.looping && loopT < rt.prevLoopTime) {
							std::fill(rt.burstFired.begin(), rt.burstFired.end(), uint8_t(0));
						}
						for (size_t bi = 0; bi < e.emission.bursts.size(); ++bi) {
							const EmissionBurst& b = e.emission.bursts[bi];
							if (!rt.burstFired[bi] && loopT >= b.time) {
								rt.burstFired[bi] = 1;
								const uint32_t bseed = Hash(rt.seedBase ^ static_cast<uint32_t>(bi * 0x9e3779b9u));
								if (RandomUnit(bseed, 777u) <= b.probability) {
									const float cnt = b.count.Evaluate(lut, normT, RandomUnit(bseed, RandomItem::BurstCount));
									const int n = static_cast<int>(cnt + 0.5f);
									for (int k = 0; k < n; ++k) Spawn(rt, e, normT, origin, world);
								}
							}
						}
						rt.prevLoopTime = loopT;
					}
				}

				// 積分 (生存粒子ごと, u = age/lifetime)
				const float   normTNow   = NormT(e, rt.playbackTime);
				const Vector3 gravityDir(0.0f, -1.0f, 0.0f);
				uint32_t i = 0;
				while (i < rt.aliveCount) {
					rt.age[i] += dt;
					if (rt.age[i] >= rt.lifetime[i]) {
						const uint32_t last = --rt.aliveCount;   // swap-remove
						if (i != last) MoveParticle(rt, i, last);
						continue;
					}
					const float u = rt.age[i] / rt.lifetime[i];

					// 重力
					const float gm = e.gravityModifier.Evaluate(lut, normTNow, RandomUnit(rt.seed[i], RandomItem::GravityModifier));
					if (gm != 0.0f) rt.velocity[i] += gravityDir * (GRAVITY * gm * dt);

					// 速度の時間変化 (追加オフセットとして加算, space は MVP では無視)
					if (e.velocityOverLifetime.enabled) {
						const float vx = e.velocityOverLifetime.linearX.Evaluate(lut, u, RandomUnit(rt.seed[i], RandomItem::VelocityX));
						const float vy = e.velocityOverLifetime.linearY.Evaluate(lut, u, RandomUnit(rt.seed[i], RandomItem::VelocityY));
						const float vz = e.velocityOverLifetime.linearZ.Evaluate(lut, u, RandomUnit(rt.seed[i], RandomItem::VelocityZ));
						rt.position[i] += Vector3(vx, vy, vz) * dt;
					}

					rt.position[i] += rt.velocity[i] * dt;

					// サイズの時間変化
					float sz = rt.initialSize[i];
					if (e.sizeOverLifetime.enabled)
						sz *= e.sizeOverLifetime.size.Evaluate(lut, u, RandomUnit(rt.seed[i], RandomItem::SizeOverLifetime));
					rt.size[i] = sz;

					// カラーの時間変化
					Vector4 col = rt.initialColor[i];
					if (e.colorOverLifetime.enabled)
						col = col * e.colorOverLifetime.Evaluate(u);
					rt.color[i] = col;

					// 回転の時間変化
					if (e.rotationOverLifetime.enabled)
						rt.rotation[i] += e.rotationOverLifetime.angularVelocity.Evaluate(lut, u, RandomUnit(rt.seed[i], RandomItem::RotationOverLifetime)) * dt;

					++i;
				}
			}
		} // namespace


		void ParticleEmitterRuntime::Init(const EmitterData& data, uint32_t emitterIndex)
		{
			const uint32_t cap = data.maxParticles > 0 ? static_cast<uint32_t>(data.maxParticles) : 1u;
			position.assign(cap, Vector3());
			velocity.assign(cap, Vector3());
			age.assign(cap, 0.0f);
			lifetime.assign(cap, 0.0f);
			seed.assign(cap, 0u);
			initialSize.assign(cap, 0.0f);
			initialColor.assign(cap, Vector4(1.0f, 1.0f, 1.0f, 1.0f));
			rotation.assign(cap, 0.0f);
			size.assign(cap, 0.0f);
			color.assign(cap, Vector4(1.0f, 1.0f, 1.0f, 1.0f));

			aliveCount      = 0;
			playbackTime    = 0.0f;
			emitAccumulator = 0.0f;
			prevLoopTime    = 0.0f;
			spawnCounter    = 0;
			seedBase        = Hash(0x9e3779b9u * (emitterIndex + 1u) + 0x1234567u);
			burstFired.assign(data.emission.bursts.size(), uint8_t(0));
		}


		void ParticleEmitterComponent::SetAsset(const char* path)
		{
			assetPath_ = path ? path : "";
			runtimes_.clear();
			if (assetPath_.empty()) {
				asset_.reset();
				state_ = State::Empty;
				return;
			}
			asset_ = aq::res::ResourceManager::Get().Load<aq::res::ParticleSystemData>(assetPath_.c_str());
			state_ = State::Loading;
		}


		void ParticleEmitterComponent::Restart()
		{
			playing_ = true;
			for (ParticleEmitterRuntime& rt : runtimes_) {
				rt.aliveCount      = 0;
				rt.playbackTime    = 0.0f;
				rt.emitAccumulator = 0.0f;
				rt.prevLoopTime    = 0.0f;
				rt.spawnCounter    = 0;
				std::fill(rt.burstFired.begin(), rt.burstFired.end(), uint8_t(0));
			}
		}


		void ParticleEmitterComponent::EnsureRuntimes()
		{
			if (state_ == State::Ready) return;
			if (!asset_) return;
			if (asset_->IsFailed()) { state_ = State::Empty; return; }
			if (!asset_->IsCompleted()) return;

			const std::vector<EmitterData>& emitters = asset_->GetEmitters();
			runtimes_.resize(emitters.size());
			for (size_t i = 0; i < emitters.size(); ++i)
				runtimes_[i].Init(emitters[i], static_cast<uint32_t>(i));
			state_ = State::Ready;
		}


		void ParticleEmitterComponent::Simulate(float dt, const aq::math::Vector3& worldOrigin)
		{
			lastWorldOrigin_ = worldOrigin;
			if (state_ != State::Ready) {
				EnsureRuntimes();
				if (state_ != State::Ready) return;
			}
			const std::vector<EmitterData>& emitters = asset_->GetEmitters();
			for (size_t i = 0; i < emitters.size() && i < runtimes_.size(); ++i)
				StepEmitter(runtimes_[i], emitters[i], dt, worldOrigin, playing_);
		}


		bool ParticleEmitterComponent::EnsureShaders()
		{
			auto& rm = aq::res::ResourceManager::Get();
			if (!particleVs_)
				particleVs_ = rm.LoadShader("Assets/Shader/Particle.fx", "VSMain", graphics::IShader::ShaderType::VS);
			if (!particlePs_)
				particlePs_ = rm.LoadShader("Assets/Shader/Particle.fx", "PSMain", graphics::IShader::ShaderType::PS);
			return particleVs_ && particleVs_->IsCompleted() && particleVs_->GetShader()
			    && particlePs_ && particlePs_->IsCompleted() && particlePs_->GetShader();
		}


		void ParticleEmitterComponent::EnsureBuffers(ParticleEmitterRenderState& rs, uint32_t quadCap)
		{
			if (rs.vb && rs.ib && rs.quadCapacity >= quadCap) return;
			if (quadCap == 0) return;

			const uint32_t vtxCount = quadCap * 4u;
			std::vector<aq::rendering::ParticleVertex> zero(vtxCount);
			rs.vb = graphics::GraphicsDevice::Get().CreateDynamicVertexBuffer(
				vtxCount, static_cast<uint32_t>(sizeof(aq::rendering::ParticleVertex)), zero.data());

			// 頂点 0=左下 1=右下 2=左上 3=右上。D3D12 は CULL_BACK + FrontCounterClockwise=FALSE
			// (CW が表) のため、カメラから見て時計回りになる巻き順で三角形を張る (裏面カリング回避)。
			std::vector<uint32_t> idx;
			idx.reserve(quadCap * 6u);
			for (uint32_t q = 0; q < quadCap; ++q) {
				const uint32_t b = q * 4u;
				idx.push_back(b + 0); idx.push_back(b + 2); idx.push_back(b + 1);
				idx.push_back(b + 2); idx.push_back(b + 3); idx.push_back(b + 1);
			}
			rs.ib = graphics::GraphicsDevice::Get().CreateIndexBuffer(
				static_cast<uint32_t>(idx.size()), idx.data());

			rs.quadCapacity = quadCap;
		}


		void ParticleEmitterComponent::FillParticleItems(
			std::vector<aq::rendering::ParticleRenderItem>& out,
			const aq::math::Vector3& camRight,
			const aq::math::Vector3& camUp)
		{
			if (state_ != State::Ready) return;
			if (!EnsureShaders()) return;

			const std::vector<EmitterData>& emitters = asset_->GetEmitters();
			if (renderStates_.size() != runtimes_.size())
				renderStates_.resize(runtimes_.size());

			std::vector<aq::rendering::ParticleVertex> verts;
			for (size_t i = 0; i < runtimes_.size() && i < emitters.size(); ++i) {
				ParticleEmitterRuntime& rt = runtimes_[i];
				if (rt.aliveCount == 0) continue;

				const EmitterData& e = emitters[i];
				const bool world = (e.simulationSpace == SimulationSpace::World);
				ParticleEmitterRenderState& rs = renderStates_[i];
				EnsureBuffers(rs, static_cast<uint32_t>(rt.position.size()));
				if (!rs.vb || !rs.ib) continue;

				verts.clear();
				verts.reserve(rt.aliveCount * 4u);
				for (uint32_t p = 0; p < rt.aliveCount; ++p) {
					const aq::math::Vector3 wpos = world ? rt.position[p]
					                                     : (lastWorldOrigin_ + rt.position[p]);
					const float h   = rt.size[p] * 0.5f;
					const float rad = rt.rotation[p] * DEG2RAD;
					const float cs  = std::cos(rad);
					const float sn  = std::sin(rad);
					const aq::math::Vector3 r = (camRight * cs + camUp * sn) * h;   // 回転後の右半径
					const aq::math::Vector3 u = (camUp * cs - camRight * sn) * h;   // 回転後の上半径
					const aq::math::Vector4& c = rt.color[p];

					aq::rendering::ParticleVertex v;
					v.color = c;
					v.position = wpos - r - u; v.uv = aq::math::Vector2(0.0f, 0.0f); verts.push_back(v);
					v.position = wpos + r - u; v.uv = aq::math::Vector2(1.0f, 0.0f); verts.push_back(v);
					v.position = wpos - r + u; v.uv = aq::math::Vector2(0.0f, 1.0f); verts.push_back(v);
					v.position = wpos + r + u; v.uv = aq::math::Vector2(1.0f, 1.0f); verts.push_back(v);
				}

				rs.vb->Update(verts.data(),
					static_cast<uint32_t>(verts.size() * sizeof(aq::rendering::ParticleVertex)));

				aq::rendering::ParticleRenderItem item;
				item.vertexBuffer = rs.vb;
				item.indexBuffer  = rs.ib;
				item.vs = std::shared_ptr<graphics::IShader>(particleVs_, particleVs_->GetShader());
				item.ps = std::shared_ptr<graphics::IShader>(particlePs_, particlePs_->GetShader());
				item.indexCount = rt.aliveCount * 6u;
				item.additive   = (e.renderer.blendMode == BlendMode::Additive);
				out.push_back(std::move(item));
			}
		}


		void ParticleSystem::Update()
		{
			const float deltaTime = aq::Engine::GetDeltaTime();
			aq::ecs::Foreach<ParticleEmitterComponent, HierarchicalTransformComponent>(
				[deltaTime](const aq::ecs::Entity&,
				            ParticleEmitterComponent*        emitter,
				            HierarchicalTransformComponent*  htc)
				{
					emitter->Simulate(deltaTime, htc->transform.position);
				});
		}


#ifdef AQ_DEBUG_IMGUI
		void ParticleSystem::RenderContent()
		{
			int      entityCount = 0, readyCount = 0, shaderCount = 0;
			uint32_t aliveTotal  = 0;
			aq::ecs::Foreach<ParticleEmitterComponent>(
				[&](const aq::ecs::Entity&, ParticleEmitterComponent* e)
				{
					++entityCount;
					if (e->IsReady())      ++readyCount;
					if (e->ShadersReady()) ++shaderCount;
					aliveTotal += e->GetAliveCount();
				});

			ImGui::Text("Emitter entities : %d", entityCount);
			ImGui::Text("Asset ready      : %d / %d", readyCount, entityCount);
			ImGui::Text("Shader ready     : %d / %d", shaderCount, entityCount);
			ImGui::Text("Alive particles  : %u", aliveTotal);

			if (ImGui::Button("Restart all"))
			{
				aq::ecs::Foreach<ParticleEmitterComponent>(
					[](const aq::ecs::Entity&, ParticleEmitterComponent* e) { e->Restart(); });
			}

			ImGui::Separator();
			aq::ecs::Foreach<ParticleEmitterComponent>(
				[](const aq::ecs::Entity&, ParticleEmitterComponent* e)
				{
					ImGui::Text("%-32s ready=%d shader=%d alive=%u",
						e->GetAssetPath().empty() ? "(no asset)" : e->GetAssetPath().c_str(),
						e->IsReady() ? 1 : 0, e->ShadersReady() ? 1 : 0, e->GetAliveCount());
				});
		}
#endif
	}
}
