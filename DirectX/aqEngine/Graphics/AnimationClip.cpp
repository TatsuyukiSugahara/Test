#include "aq.h"
#include "AnimationClip.h"
#include <DirectXMath.h>
#include <cmath>


namespace aq
{
	namespace graphics
	{
		void AnimationClip::Initialize(res::RefAnimationResource resource)
		{
			resource_ = resource;
		}


		float AnimationClip::GetDuration() const
		{
			if (!resource_ || !resource_->IsCompleted()) return 0.0f;
			return resource_->GetDuration();
		}


		bool AnimationClip::IsValid() const
		{
			return resource_ != nullptr && !resource_->IsFailed();
		}


		bool AnimationClip::IsLoaded() const
		{
			return resource_ && resource_->IsCompleted();
		}


		void AnimationClip::CalcBoneMatrices(float                             time,
		                                      const std::vector<res::BoneData>& bones,
		                                      std::vector<math::Matrix4x4>&     outMatrices) const
		{
			if (!IsLoaded()) return;

			const res::AnimationClipData* clipData = resource_->GetClipData();
			if (!clipData) return;

			const uint32_t boneCount = static_cast<uint32_t>(bones.size());
			if (boneCount == 0) return;

			// 時刻をループさせる
			float t = time;
			if (clipData->duration > 0.0f) {
				t = std::fmod(t, clipData->duration);
				if (t < 0.0f) t += clipData->duration;
			}

			// ─── Step 1: ローカル変換行列をサンプリング ───────────────────────
			std::vector<math::Matrix4x4> localMatrices(boneCount, math::Matrix4x4::Identity);

			for (uint32_t boneNo = 0; boneNo < boneCount; ++boneNo) {
				if (boneNo >= clipData->boneKeyframes.size()) continue;
				const auto& kfs = clipData->boneKeyframes[boneNo];
				if (kfs.empty()) continue;

				// 時刻 t を挟む 2 つのキーフレームを探す
				int prevIdx = 0;
				int nextIdx = 0;
				if (kfs.size() > 1) {
					if (t <= kfs.front().time) {
						prevIdx = 0;
						nextIdx = 0;
					}
					else if (t >= kfs.back().time) {
						prevIdx = static_cast<int>(kfs.size()) - 1;
						nextIdx = prevIdx;
					}
					else {
						nextIdx = 1;
						while (nextIdx < static_cast<int>(kfs.size()) && kfs[nextIdx].time < t) {
							++nextIdx;
						}
						if (nextIdx >= static_cast<int>(kfs.size())) {
							nextIdx = static_cast<int>(kfs.size()) - 1;
						}
						prevIdx = nextIdx - 1;
					}
				}

				math::Vector3    trans = kfs[prevIdx].translation;
				math::Quaternion rot   = kfs[prevIdx].rotation;
				math::Vector3    scale = kfs[prevIdx].scale;

				if (prevIdx != nextIdx) {
					const float dt = kfs[nextIdx].time - kfs[prevIdx].time;
					const float alpha = (dt > 0.0f)
						? std::clamp((t - kfs[prevIdx].time) / dt, 0.0f, 1.0f)
						: 0.0f;

					// 線形補間 (位置・スケール)
					trans.x = kfs[prevIdx].translation.x + alpha * (kfs[nextIdx].translation.x - kfs[prevIdx].translation.x);
					trans.y = kfs[prevIdx].translation.y + alpha * (kfs[nextIdx].translation.y - kfs[prevIdx].translation.y);
					trans.z = kfs[prevIdx].translation.z + alpha * (kfs[nextIdx].translation.z - kfs[prevIdx].translation.z);
					scale.x = kfs[prevIdx].scale.x + alpha * (kfs[nextIdx].scale.x - kfs[prevIdx].scale.x);
					scale.y = kfs[prevIdx].scale.y + alpha * (kfs[nextIdx].scale.y - kfs[prevIdx].scale.y);
					scale.z = kfs[prevIdx].scale.z + alpha * (kfs[nextIdx].scale.z - kfs[prevIdx].scale.z);

					// 球面線形補間 (回転クォータニオン)
					DirectX::XMVECTOR q0 = DirectX::XMLoadFloat4(
						reinterpret_cast<const DirectX::XMFLOAT4*>(&kfs[prevIdx].rotation));
					DirectX::XMVECTOR q1 = DirectX::XMLoadFloat4(
						reinterpret_cast<const DirectX::XMFLOAT4*>(&kfs[nextIdx].rotation));
					q0 = DirectX::XMQuaternionNormalize(q0);
					q1 = DirectX::XMQuaternionNormalize(q1);
					if (DirectX::XMVectorGetX(DirectX::XMVector4Dot(q0, q1)) < 0.0f) {
						q1 = DirectX::XMVectorNegate(q1);
					}
					const DirectX::XMVECTOR qr = DirectX::XMQuaternionNormalize(
						DirectX::XMQuaternionSlerp(q0, q1, alpha));
					DirectX::XMStoreFloat4(reinterpret_cast<DirectX::XMFLOAT4*>(&rot), qr);
				}

				// ローカル行列: Scale * Rotate * Translate
				math::Matrix4x4 scaleMat, rotMat, transMat;
				scaleMat.MakeScaling(scale);
				rotMat.MakeRotationFromQuaternion(rot);
				transMat.MakeTranslation(trans);
				localMatrices[boneNo].Mull(scaleMat, rotMat);
				localMatrices[boneNo].Mull(localMatrices[boneNo], transMat);
			}

			// ─── Step 2: グローバル行列を累積 (親が子より前に並んでいる前提) ───
			outMatrices.resize(boneCount);
			for (uint32_t boneNo = 0; boneNo < boneCount; ++boneNo) {
				if (bones[boneNo].parentIndex < 0) {
					outMatrices[boneNo] = localMatrices[boneNo];
				} else {
					outMatrices[boneNo].Mull(
						localMatrices[boneNo],
						outMatrices[static_cast<uint32_t>(bones[boneNo].parentIndex)]);
				}
			}

			// ─── Step 3: 逆バインドポーズを乗算してスキニング行列を得る ────────
			for (uint32_t boneNo = 0; boneNo < boneCount; ++boneNo) {
				math::Matrix4x4 skinMatrix;
				skinMatrix.Mull(bones[boneNo].inverseBindPose, outMatrices[boneNo]);
				outMatrices[boneNo] = skinMatrix;
			}
		}
	}
}
