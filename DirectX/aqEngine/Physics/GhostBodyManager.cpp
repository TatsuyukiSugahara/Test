#include "aq.h"
#include "GhostBodyManager.h"
#include "GhostBody.h"
#include "BulletDbvtBroadphase.h"
#include "BulletGhostNarrowphase.h"
#include <cassert>


namespace aq
{
	namespace physics
	{
		GhostBodyManager* GhostBodyManager::instance_ = nullptr;


		void GhostBodyManager::Initialize(
			std::unique_ptr<IBroadphase>      broadphase,
			std::unique_ptr<IGhostNarrowphase> narrowphase)
		{
			assert(!instance_ && "GhostBodyManager::Initialize called twice");
			instance_ = new GhostBodyManager();
			if (broadphase)  instance_->broadphase_  = std::move(broadphase);
			if (narrowphase) instance_->narrowphase_ = std::move(narrowphase);
		}


		void GhostBodyManager::Finalize()
		{
			if (instance_) {
				delete instance_;
				instance_ = nullptr;
			}
		}


		GhostBodyManager& GhostBodyManager::Get()
		{
			assert(instance_ && "GhostBodyManager not initialized");
			return *instance_;
		}


		GhostBodyManager::GhostBodyManager()
		{
			// デフォルトは Bullet 実装
			broadphase_  = std::make_unique<BulletDbvtBroadphase>();
			narrowphase_ = std::make_unique<BulletGhostNarrowphase>();
		}


		GhostBodyManager::~GhostBodyManager()
		{
			// narrowphase が各ボディの OnBodyRemoved を自分のデストラクタで処理するので
			// ここでは bodyList_ を空にするだけでよい
			bodyList_.clear();
			narrowphase_.reset();
			broadphase_.reset();
		}


		void GhostBodyManager::AddBody(GhostBody* body)
		{
			if (!body) return;
			if (std::find(bodyList_.begin(), bodyList_.end(), body) != bodyList_.end()) return;

			bodyList_.push_back(body);
			broadphase_->Add(body);
			narrowphase_->OnBodyAdded(body);
		}


		void GhostBodyManager::RemoveBody(GhostBody* body)
		{
			if (!body) return;

			auto it = std::find(bodyList_.begin(), bodyList_.end(), body);
			if (it == bodyList_.end()) return;

			bodyList_.erase(it);

			if (body->GetBroadphaseHandle()) {
				broadphase_->Remove(body);
			}
			narrowphase_->OnBodyRemoved(body);
		}


		void GhostBodyManager::Update()
		{
			for (auto* body : bodyList_) {
				if (body->IsDirty()) {
					broadphase_->Update(body);
					body->ClearDirty();
				}
			}

			broadphase_->Perform([this](GhostBody* a, GhostBody* b) {
				ProcessCollisionPair(a, b);
			});
		}


		void GhostBodyManager::ProcessCollisionPair(GhostBody* a, GhostBody* b)
		{
			if (a == b) return;
			if (!a->IsActive() || !b->IsActive()) return;

			// 属性マスクフィルタリング
			if (!((a->GetMask() & b->GetAttribute()) && (b->GetMask() & a->GetAttribute()))) return;

			// 形状タイプID順に並べ替え（重複実装排除）
			if (a->GetShapeType() > b->GetShapeType()) {
				std::swap(a, b);
			}

			// 包含球事前チェック（Broadphase の候補をさらに絞り込む）
			math::Vector3 diff = a->GetPosition() - b->GetPosition();
			float distSq = diff.LengthSq();
			float sumR   = a->GetBoundingRadius() + b->GetBoundingRadius();
			if (distSq > sumR * sumR) return;

			// Sphere vs Sphere は包含球チェックで確定
			bool isHit = (a->GetShapeType() == GhostShapeType::Sphere &&
			              b->GetShapeType() == GhostShapeType::Sphere)
			             ? true
			             : narrowphase_->CheckCollision(a, b);

			if (isHit && pairCallback_) {
				pairCallback_(a, b);
			}
		}
	}
}
