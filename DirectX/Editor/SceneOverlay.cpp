#include "stdafx.h"
#include "SceneOverlay.h"
#include <algorithm>
#include <cmath>


namespace editor
{
	namespace
	{
		/** グリッドの間隔を切り替えるカメラの高さ(これ未満なら小さい方の間隔) */
		static constexpr float HEIGHT_FOR_10M  = 10.0f;
		static constexpr float HEIGHT_FOR_100M = 100.0f;

		/** 範囲は max(|高さ| × RANGE_PER_HEIGHT, MIN_RANGE) */
		static constexpr float RANGE_PER_HEIGHT = 10.0f;
		static constexpr float MIN_RANGE        = 50.0f;

		/** 太い線(この本数ごと) */
		static constexpr int MAJOR_LINE_INTERVAL = 10;

		/** 線の色 */
		static constexpr ImU32 MINOR_COLOR  = IM_COL32(255, 255, 255, 28);
		static constexpr ImU32 MAJOR_COLOR  = IM_COL32(255, 255, 255, 64);
		static constexpr ImU32 X_AXIS_COLOR = IM_COL32(230, 70, 70, 220);
		static constexpr ImU32 Z_AXIS_COLOR = IM_COL32(70, 110, 240, 220);
		static constexpr float AXIS_THICKNESS = 1.5f;

		/** クリップ空間の同次座標の点 */
		struct ClipPoint
		{
			float x, y, z, w;
		};


		ClipPoint ToClip(const aq::Camera& camera, const float (&p)[3])
		{
			const aq::math::Vector4 clip = camera.WorldToClip(aq::math::Vector3(p[0], p[1], p[2]));
			return { clip.x, clip.y, clip.z, clip.w };
		}


		/** 2 点の間で z が 0 になる点(from.z と to.z の符号が違うときだけ呼ぶ) */
		ClipPoint LerpToNearPlane(const ClipPoint& from, const ClipPoint& to)
		{
			const float t = from.z / (from.z - to.z);
			return { from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t, 0.0f, from.w + (to.w - from.w) * t };
		}
	}


	bool SceneOverlay::DrawWorldLine(ImDrawList* drawList, const aq::Camera& camera, const ImVec2& imageMin, const ImVec2& imageMax,
	                                 const float (&from)[3], const float (&to)[3], const ImU32 color, const float thickness)
	{
		ClipPoint a = ToClip(camera, from);
		ClipPoint b = ToClip(camera, to);

		// 近い面(z = 0)で切る。両方が手前なら描かない
		if (a.z < 0.0f && b.z < 0.0f) {
			return false;
		}
		if (a.z < 0.0f) {
			a = LerpToNearPlane(a, b);
		} else if (b.z < 0.0f) {
			b = LerpToNearPlane(b, a);
		}
		// z >= 0 の透視投影なら w は近い面の距離以上で正。正射影や特異な行列で 0 以下なら描かない
		if (a.w <= 1e-6f || b.w <= 1e-6f) {
			return false;
		}

		const float width  = imageMax.x - imageMin.x;
		const float height = imageMax.y - imageMin.y;
		const float ax = a.x / a.w, ay = a.y / a.w;
		const float bx = b.x / b.w, by = b.y / b.w;
		drawList->AddLine(ImVec2(imageMin.x + (ax * 0.5f + 0.5f) * width, imageMin.y + (0.5f - ay * 0.5f) * height),
		                  ImVec2(imageMin.x + (bx * 0.5f + 0.5f) * width, imageMin.y + (0.5f - by * 0.5f) * height),
		                  color, thickness);
		return true;
	}


	void SceneOverlay::DrawGrid(ImDrawList* drawList, const aq::Camera& camera, const ImVec2& imageMin, const ImVec2& imageMax)
	{
		if (imageMax.x <= imageMin.x || imageMax.y <= imageMin.y) {
			return;
		}

		const aq::math::Vector3& eye = camera.GetPosition();
		const float height = std::fabs(eye.y);
		const float range  = std::max(height * RANGE_PER_HEIGHT, MIN_RANGE);
		const float step   = (height < HEIGHT_FOR_10M) ? 1.0f : ((height < HEIGHT_FOR_100M) ? 10.0f : 100.0f);

		// カメラの真下を中心に、間隔の倍数の位置へ揃える(動いても線が滑らない)
		const int minIndexX = static_cast<int>(std::floor((eye.x - range) / step));
		const int maxIndexX = static_cast<int>(std::ceil((eye.x + range) / step));
		const int minIndexZ = static_cast<int>(std::floor((eye.z - range) / step));
		const int maxIndexZ = static_cast<int>(std::ceil((eye.z + range) / step));
		const float minX = static_cast<float>(minIndexX) * step;
		const float maxX = static_cast<float>(maxIndexX) * step;
		const float minZ = static_cast<float>(minIndexZ) * step;
		const float maxZ = static_cast<float>(maxIndexZ) * step;

		drawList->PushClipRect(imageMin, imageMax, true);

		// Z 方向へ伸びる線(x 一定)。x = 0 は軸として後で描く
		for (int i = minIndexX; i <= maxIndexX; ++i) {
			if (i == 0) continue;
			const float x = static_cast<float>(i) * step;
			const float from[3] = { x, 0.0f, minZ };
			const float to[3]   = { x, 0.0f, maxZ };
			DrawWorldLine(drawList, camera, imageMin, imageMax, from, to, (i % MAJOR_LINE_INTERVAL == 0) ? MAJOR_COLOR : MINOR_COLOR, 1.0f);
		}
		// X 方向へ伸びる線(z 一定)。z = 0 は軸として後で描く
		for (int i = minIndexZ; i <= maxIndexZ; ++i) {
			if (i == 0) continue;
			const float z = static_cast<float>(i) * step;
			const float from[3] = { minX, 0.0f, z };
			const float to[3]   = { maxX, 0.0f, z };
			DrawWorldLine(drawList, camera, imageMin, imageMax, from, to, (i % MAJOR_LINE_INTERVAL == 0) ? MAJOR_COLOR : MINOR_COLOR, 1.0f);
		}

		// 原点の軸。グリッドの範囲と、原点を含む範囲の長さで描く
		{
			const float xFrom[3] = { std::min(minX, 0.0f), 0.0f, 0.0f };
			const float xTo[3]   = { std::max(maxX, 0.0f), 0.0f, 0.0f };
			DrawWorldLine(drawList, camera, imageMin, imageMax, xFrom, xTo, X_AXIS_COLOR, AXIS_THICKNESS);
			const float zFrom[3] = { 0.0f, 0.0f, std::min(minZ, 0.0f) };
			const float zTo[3]   = { 0.0f, 0.0f, std::max(maxZ, 0.0f) };
			DrawWorldLine(drawList, camera, imageMin, imageMax, zFrom, zTo, Z_AXIS_COLOR, AXIS_THICKNESS);
		}

		drawList->PopClipRect();
	}
}
