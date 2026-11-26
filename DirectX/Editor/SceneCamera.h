#pragma once
#include <imgui/imgui.h>


namespace aq
{
	class Camera;
}


namespace editor
{
	/**
	 * シーンビュー(Edit 中のゲームビュー)の編集用カメラ。
	 * 位置・ヨー・ピッチ・注視点までの距離・移動速度を持つ。入力は OnBuildUI(エディタ用の NewFrame の後)で読んで状態を更新し、
	 * メインカメラへ書くのは次のフレームの OnBeginFrame(ApplyTo。Edit のときだけ)。
	 * 操作は設計書/エディタ/E11_シーンビュー.md 2. のとおり(F キーは E11b)。座標系は左手系・Y 上、ヨー 0 で +Z を向く
	 */
	class SceneCamera
	{
	private:
		/** ドラッグ操作の種類 */
		enum class DragMode : uint8_t
		{
			None,
			Look,       // 右ドラッグ: 見回す(飛行)
			Pan,        // 中ドラッグ: 視点の面の上で平行移動
			Orbit,      // Alt + 左ドラッグ: 注視点の周りを回る
		};


	private:
		/** 状態 */
		float positionX_ = 0.0f, positionY_ = 0.0f, positionZ_ = 0.0f;
		float yaw_       = 0.0f;     // [rad] 0 で +Z、正で +X 側へ回る
		float pitch_     = 0.0f;     // [rad] 正で上向き(±89° に収める)
		float distance_  = 1.0f;     // 注視点までの距離
		float moveSpeed_ = 10.0f;    // 飛行の速さ [m/s]

		/** 投影 */
		float viewAngle_ = 0.0f;     // [rad]
		float near_      = 0.1f;
		float far_       = 1000.0f;

		/** 今のドラッグ */
		DragMode dragMode_ = DragMode::None;


	public:
		/** 起動時の視点(位置 (0, 10, -20)、注視点は原点、画角 60°、near 0.1、far 1000) */
		SceneCamera();


	public:
		/**
		 * 入力を読んで状態を更新する(Edit のときだけ OnBuildUI から呼ぶ)。エディタ用コンテキストが current であること。
		 * @param isHovered       ゲームビューの画像の上にマウスがある(ドラッグの開始とホイールに使う)
		 * @param viewportHeight  画像の高さ [px](平行移動の速さの換算に使う)
		 */
		void OnBuildUI(const bool isHovered, const float viewportHeight);

		/** ドラッグを取り消す(Edit でなくなった・画像が見えなくなったとき) */
		void CancelDrag() { dragMode_ = DragMode::None; }

		/** メインカメラへ書く(OnBeginFrame から、Edit のときだけ)。アスペクト比は変えない */
		void ApplyTo(aq::Camera& camera) const;


	public:
		/** 右ドラッグ中(飛行中)か。GameViewInput がキーの転送を止める判定に使う */
		inline bool IsFlying() const { return dragMode_ == DragMode::Look; }

		/** 何かのドラッグ操作中か */
		inline bool IsDragging() const { return dragMode_ != DragMode::None; }

		inline float GetMoveSpeed() const { return moveSpeed_; }
		inline float GetPositionX() const { return positionX_; }
		inline float GetPositionY() const { return positionY_; }
		inline float GetPositionZ() const { return positionZ_; }


	private:
		/** 向きの基底(前・右・上)を求める */
		void GetBasis(float (&forward)[3], float (&right)[3], float (&up)[3]) const;

		/** 見回す・回る共通の角度の更新(ピッチを ±89° に収める) */
		void AddAngles(const float deltaYaw, const float deltaPitch);
	};
}
