#include "aq.h"
#include "DecalComponent.h"
#include "Graphics/IShaderResourceView.h"
#include <cmath>


namespace aq
{
	namespace ecs
	{
		void DecalComponent::SetTexturePath(const char* path)
		{
			texturePath_ = path ? path : "";
			texture_.reset();
			loadRequested_ = false;
			state_ = texturePath_.empty() ? State::Invalid : State::LoadRequest;
		}


		void DecalComponent::Update()
		{
			switch (state_)
			{
				case State::Invalid:
					break;

				case State::LoadRequest:
					texture_       = aq::res::ResourceManager::Get().Load<aq::res::GPUResource>(texturePath_.c_str());
					loadRequested_ = true;
					state_         = State::Loading;
					[[fallthrough]];

				case State::Loading:
					if (!texture_ || texture_->IsFailed())
					{
						texture_.reset();
						state_ = State::Invalid;
						break;
					}
					if (!texture_->IsCompleted())
						break;
					state_ = State::Ready;
					break;

				case State::Ready:
					break;
			}
		}


		bool DecalComponent::FillDecalItem(aq::rendering::DecalRenderItem& out,
		                                   const aq::ecs::Transform& xf) const
		{
			if (state_ != State::Ready || !texture_)
				return false;

			graphics::IShaderResourceView* srv = texture_->GetShaderResourceView();
			if (!srv)
				return false;

			// decalToWorld = Scale * Rot * Trans (行ベクトル規約)
			aq::math::Matrix4x4 scaleMat, rotMat, transMat, srMat, decalToWorld;
			scaleMat.MakeScaling(aq::math::Vector3(
				size_.x * xf.scale.x, size_.y * xf.scale.y, size_.z * xf.scale.z));
			rotMat.MakeRotationFromQuaternion(xf.rotation);
			transMat.MakeTranslation(xf.position);
			srMat.Mull(scaleMat, rotMat);
			decalToWorld.Mull(srMat, transMat);

			// ワールド → デカールローカル
			out.cb.worldToDecal.Inverse(decalToWorld);

			// 投影軸 = ローカル -Y のワールド方向 = -(decalToWorld の 2 行目) (正規化)
			// 無回転なら真下 (-Y) に投影され、地面に水平に貼られる。
			aq::math::Vector3 fwd(-decalToWorld._21, -decalToWorld._22, -decalToWorld._23);
			const float len = std::sqrt(fwd.x * fwd.x + fwd.y * fwd.y + fwd.z * fwd.z);
			if (len > 1e-6f) { fwd.x /= len; fwd.y /= len; fwd.z /= len; }

			out.cb.forward = aq::math::Vector4(fwd.x, fwd.y, fwd.z, angleFadeMin_);
			out.cb.color   = aq::math::Vector4(color_.x, color_.y, color_.z, opacity_);

			// GPUResource の寿命をエイリアシング shared_ptr で SRV に繋ぐ
			out.texture = std::shared_ptr<graphics::IShaderResourceView>(texture_, srv);
			return true;
		}
	}
}
