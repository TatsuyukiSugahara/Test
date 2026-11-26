#include "stdafx.h"
#include "SceneCamera.h"
#include <imgui/imgui.h>
#include <algorithm>
#include <cmath>


namespace editor
{
	namespace
	{
		/** 起動時の視点 */
		static constexpr float INITIAL_POSITION_X = 0.0f;
		static constexpr float INITIAL_POSITION_Y = 10.0f;
		static constexpr float INITIAL_POSITION_Z = -20.0f;

		/** 投影 */
		static constexpr float VIEW_ANGLE_DEGREE = 60.0f;
		static constexpr float NEAR_PLANE        = 0.1f;
		static constexpr float FAR_PLANE         = 1000.0f;

		/** ピッチの限界(±89°) */
		static constexpr float MAX_PITCH = 89.0f * 3.14159265358979f / 180.0f;

		/** マウスの 1 px あたりに回る角度 [rad] */
		static constexpr float LOOK_RADIAN_PER_PIXEL = 0.005f;

		/** 飛行の速さの範囲 [m/s]、Shift の倍率、ホイール 1 ノッチあたりの倍率(2 の累乗) */
		static constexpr float MIN_MOVE_SPEED   = 0.1f;
		static constexpr float MAX_MOVE_SPEED   = 1000.0f;
		static constexpr float SHIFT_MULTIPLIER = 4.0f;

		/** 注視点までの距離の範囲と、ホイール 1 ノッチあたりの倍率 */
		static constexpr float MIN_DISTANCE         = 0.1f;
		static constexpr float MAX_DISTANCE         = 5000.0f;
		static constexpr float ZOOM_RATIO_PER_NOTCH = 0.9f;

		/** F で寄るとき、境界の無いノードとの距離 [m] */
		static constexpr float FOCUS_DISTANCE_WITHOUT_BOUNDS = 5.0f;

		/** 1 フレームの時間の上限(止まった後の飛びすぎを避ける) */
		static constexpr float MAX_DELTA_TIME = 0.1f;


		float Clamp(const float value, const float minValue, const float maxValue)
		{
			return std::max(minValue, std::min(maxValue, value));
		}
	}


	SceneCamera::SceneCamera()
		: positionX_(INITIAL_POSITION_X)
		, positionY_(INITIAL_POSITION_Y)
		, positionZ_(INITIAL_POSITION_Z)
		, viewAngle_(VIEW_ANGLE_DEGREE * 3.14159265358979f / 180.0f)
		, near_(NEAR_PLANE)
		, far_(FAR_PLANE)
	{
		// 原点を見る向きと距離
		const float dx = -positionX_;
		const float dy = -positionY_;
		const float dz = -positionZ_;
		const float horizontal = std::sqrt(dx * dx + dz * dz);
		yaw_      = std::atan2(dx, dz);
		pitch_    = std::atan2(dy, horizontal);
		distance_ = std::sqrt(dx * dx + dy * dy + dz * dz);
	}


	void SceneCamera::GetBasis(float (&forward)[3], float (&right)[3], float (&up)[3]) const
	{
		const float sy = std::sin(yaw_);
		const float cy = std::cos(yaw_);
		const float sp = std::sin(pitch_);
		const float cp = std::cos(pitch_);
		// 左手系: right = up × forward、up = forward × right
		forward[0] = cp * sy;  forward[1] = sp;  forward[2] = cp * cy;
		right[0]   = cy;       right[1]   = 0.0f; right[2]  = -sy;
		up[0]      = -sp * sy; up[1]      = cp;  up[2]      = -sp * cy;
	}


	void SceneCamera::AddAngles(const float deltaYaw, const float deltaPitch)
	{
		yaw_  += deltaYaw;
		pitch_ = Clamp(pitch_ + deltaPitch, -MAX_PITCH, MAX_PITCH);
	}


	void SceneCamera::OnBuildUI(const bool isHovered, const float viewportHeight)
	{
		ImGuiIO& io = ImGui::GetIO();

		// ドラッグの開始。ほかの操作中は始めない
		if (dragMode_ == DragMode::None && isHovered) {
			if (io.MouseClicked[ImGuiMouseButton_Right]) {
				dragMode_ = DragMode::Look;
			} else if (io.MouseClicked[ImGuiMouseButton_Middle]) {
				dragMode_ = DragMode::Pan;
			} else if (io.MouseClicked[ImGuiMouseButton_Left] && io.KeyAlt) {
				dragMode_ = DragMode::Orbit;
			}
		}

		// ドラッグの終了(開始したボタンを離したら)
		if (dragMode_ == DragMode::Look && !io.MouseDown[ImGuiMouseButton_Right])  dragMode_ = DragMode::None;
		if (dragMode_ == DragMode::Pan   && !io.MouseDown[ImGuiMouseButton_Middle]) dragMode_ = DragMode::None;
		if (dragMode_ == DragMode::Orbit && !io.MouseDown[ImGuiMouseButton_Left])   dragMode_ = DragMode::None;

		float forward[3], right[3], up[3];
		GetBasis(forward, right, up);

		const float deltaTime = std::min(io.DeltaTime, MAX_DELTA_TIME);

		switch (dragMode_) {
		case DragMode::Look: {
			// 見回す(位置は動かさない。注視点は向きに追従する)
			AddAngles(io.MouseDelta.x * LOOK_RADIAN_PER_PIXEL, -io.MouseDelta.y * LOOK_RADIAN_PER_PIXEL);

			// ホイールで移動速度(倍々)
			if (io.MouseWheel != 0.0f) {
				moveSpeed_ = Clamp(moveSpeed_ * std::pow(2.0f, io.MouseWheel), MIN_MOVE_SPEED, MAX_MOVE_SPEED);
			}

			// WASDQE で飛ぶ(向きを変えた後の基底で)
			GetBasis(forward, right, up);
			float move[3] = { 0.0f, 0.0f, 0.0f };
			if (!io.WantTextInput) {
				const float front = (ImGui::IsKeyDown(ImGuiKey_W) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_S) ? 1.0f : 0.0f);
				const float side  = (ImGui::IsKeyDown(ImGuiKey_D) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_A) ? 1.0f : 0.0f);
				const float lift  = (ImGui::IsKeyDown(ImGuiKey_E) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_Q) ? 1.0f : 0.0f);
				move[0] = forward[0] * front + right[0] * side;
				move[1] = forward[1] * front + right[1] * side + lift;   // Q / E は世界の下上
				move[2] = forward[2] * front + right[2] * side;
			}
			const float length = std::sqrt(move[0] * move[0] + move[1] * move[1] + move[2] * move[2]);
			if (length > 0.0f) {
				const float speed = moveSpeed_ * (io.KeyShift ? SHIFT_MULTIPLIER : 1.0f) * deltaTime / length;
				positionX_ += move[0] * speed;
				positionY_ += move[1] * speed;
				positionZ_ += move[2] * speed;
			}
			break;
		}

		case DragMode::Pan: {
			// 視点の面の上で動かす。1 px がちょうど注視点の位置で 1 px に見える長さ
			const float height = std::max(viewportHeight, 1.0f);
			const float perPixel = 2.0f * distance_ * std::tan(viewAngle_ * 0.5f) / height;
			const float moveRight = -io.MouseDelta.x * perPixel;
			const float moveUp    =  io.MouseDelta.y * perPixel;
			positionX_ += right[0] * moveRight + up[0] * moveUp;
			positionY_ += right[1] * moveRight + up[1] * moveUp;
			positionZ_ += right[2] * moveRight + up[2] * moveUp;
			break;
		}

		case DragMode::Orbit: {
			// 注視点を固定して回る
			const float targetX = positionX_ + forward[0] * distance_;
			const float targetY = positionY_ + forward[1] * distance_;
			const float targetZ = positionZ_ + forward[2] * distance_;
			AddAngles(io.MouseDelta.x * LOOK_RADIAN_PER_PIXEL, -io.MouseDelta.y * LOOK_RADIAN_PER_PIXEL);
			GetBasis(forward, right, up);
			positionX_ = targetX - forward[0] * distance_;
			positionY_ = targetY - forward[1] * distance_;
			positionZ_ = targetZ - forward[2] * distance_;
			break;
		}

		case DragMode::None:
			// ホイールで注視点へ寄る・離れる(注視点は動かさない)
			if (isHovered && io.MouseWheel != 0.0f) {
				const float targetX = positionX_ + forward[0] * distance_;
				const float targetY = positionY_ + forward[1] * distance_;
				const float targetZ = positionZ_ + forward[2] * distance_;
				distance_ = Clamp(distance_ * std::pow(ZOOM_RATIO_PER_NOTCH, io.MouseWheel), MIN_DISTANCE, MAX_DISTANCE);
				positionX_ = targetX - forward[0] * distance_;
				positionY_ = targetY - forward[1] * distance_;
				positionZ_ = targetZ - forward[2] * distance_;
			}
			break;
		}
	}


	void SceneCamera::Focus(const aq::math::Vector3& center, const float radius, const float aspect)
	{
		float distance = FOCUS_DISTANCE_WITHOUT_BOUNDS;
		if (radius > 0.0f) {
			// 縦の画角と、アスペクト比から求めた横の画角の狭い方で、球が収まる距離
			const float halfVertical   = viewAngle_ * 0.5f;
			const float halfHorizontal = std::atan(std::tan(halfVertical) * std::max(aspect, 1e-3f));
			const float halfAngle      = std::min(halfVertical, halfHorizontal);
			distance = radius / std::max(std::sin(halfAngle), 1e-3f);
		}
		distance_ = Clamp(distance, MIN_DISTANCE, MAX_DISTANCE);

		float forward[3], right[3], up[3];
		GetBasis(forward, right, up);
		positionX_ = center.x - forward[0] * distance_;
		positionY_ = center.y - forward[1] * distance_;
		positionZ_ = center.z - forward[2] * distance_;
	}


	void SceneCamera::ApplyTo(aq::Camera& camera) const
	{
		float forward[3], right[3], up[3];
		GetBasis(forward, right, up);

		camera.SetPosition(aq::math::Vector3(positionX_, positionY_, positionZ_));
		camera.SetTarget(aq::math::Vector3(positionX_ + forward[0] * distance_,
		                                   positionY_ + forward[1] * distance_,
		                                   positionZ_ + forward[2] * distance_));
		camera.SetUp(aq::math::Vector3(0.0f, 1.0f, 0.0f));
		camera.SetViewAngle(viewAngle_);
		camera.SetNear(near_);
		camera.SetFar(far_);
	}
}
