#pragma once
#include "Math/Bounds.h"
#include "Math/Matrix.h"

namespace aq
{
	namespace rendering
	{
		/**
		 * オクリュージョン判定インターフェース。
		 * ゲームスレッド (BuildRenderFrame) が、Hi-Z を持つレンダラへ問い合わせるために使う。
		 * 実装は別スレッドが更新する Hi-Z を内部でスレッドセーフに参照する。
		 */
		class IOcclusionTester
		{
		public:
			virtual ~IOcclusionTester() = default;

			/**
			 * ワールド AABB がオクルードされているか (画面上で他の不透明物の背後に完全に隠れているか)。
			 * 判定不能 (Hi-Z 未取得・near 跨ぎ・画面外) のときは false (= 可視扱い・保守的)。
			 * viewProj = view * projection、nearZ/farZ はカメラの近遠平面。
			 */
			virtual bool IsOccluded(const math::AABB& worldBox,
			                        const math::Matrix4x4& viewProj,
			                        float nearZ, float farZ) const = 0;
		};
	}
}
