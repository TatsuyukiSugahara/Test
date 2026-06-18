#pragma once
#include <memory>
#include <vector>
#include "Resource/Resource.h"
#include "Math/Matrix.h"


namespace aq
{
	namespace graphics
	{
		/**
		 * アニメーションクリップ
		 *
		 * AnimationResource (TKA ファイル) を参照し、指定時刻のスキニング行列を計算する。
		 * 計算されたボーン行列は AnimationComponent が SkeletalMesh に渡す。
		 *
		 * ボーン行列の計算手順:
		 *  1. 各ボーンのキーフレームをサンプリングしてローカル変換を求める (線形補間)
		 *  2. ルートから末端へ向かってグローバル行列を累積する
		 *  3. 逆バインドポーズ行列を乗算してスキニング行列を得る
		 */
		class AnimationClip
		{
		public:
			AnimationClip() = default;
			~AnimationClip() = default;

			void Initialize(res::RefAnimationResource resource);

			/**
			 * 指定時刻のスキニング行列を計算して outMatrices に書き込む。
			 * bones: SkeletalMeshResource::GetBones() で得たボーン配列 (逆バインドポーズ含む)
			 * outMatrices: boneCount 個の行列が書き込まれる
			 */
			void CalcBoneMatrices(float                            time,
			                      const std::vector<res::BoneData>& bones,
			                      std::vector<math::Matrix4x4>&     outMatrices) const;

			float GetDuration() const;
			bool  IsValid()     const;
			bool  IsLoaded()    const;

		private:
			res::RefAnimationResource resource_;
		};
	}
}
