#pragma once
#include "CapsuleCollider.h"
#include "BulletPhysics.h"


namespace engine
{
	namespace physics
	{
		/**
		 * カプセル形状を使ったキャラクター用コリジョンコントローラー。
		 *
		 * Execute() に「目標座標」を渡すと、壁とのスイープテストを繰り返して
		 * 壁にめり込まない最終座標を返す。Y 軸移動（重力）は呼び出し側で管理する。
		 *
		 * 使い方:
		 *   controller.Init(0.5f, 1.0f, startPos);
		 *   // 毎フレーム
		 *   pos = controller.Execute(intendedPos, deltaTime);
		 */
		class CharacterController
		{
		public:
			CharacterController();
			~CharacterController();

			CharacterController(const CharacterController&) = delete;
			CharacterController& operator=(const CharacterController&) = delete;


			/**
			 * @param radius             カプセル半径
			 * @param height             カプセル全体の高さ（半球部含む）
			 * @param position           初期座標
			 * @param collisionAttribute 当たり判定属性
			 */
			void Init(float radius, float height, const math::Vector3& position,
			          uint32_t collisionAttribute = kCollisionAttrAll);

			/**
			 * 毎フレーム呼ぶ。壁スイープで解決した最終座標を返す。
			 * テレポートリクエストがある場合は判定をスキップして強制移動する。
			 */
			const math::Vector3& Execute(const math::Vector3& targetPosition, float deltaTime);

			/**
			 * 次の Execute() で衝突判定をスキップして targetPosition へ移動する。
			 */
			void RequestTeleport() { isRequestTeleport_ = true; }


			const math::Vector3& GetPosition()     const { return position_; }
			const math::Vector3& GetPrevPosition() const { return prevPosition_; }
			float                GetVerticalVelocity() const { return verticalVelocity_; }

			void SetPosition(const math::Vector3& pos) { position_ = pos; }
			void SetGravity (float gravity)            { gravity_  = gravity; }

			BulletCapsuleCollider* GetCollider()  { return &collider_; }
			BulletRigidBody*       GetRigidBody() { return &rigidBody_; }

			void RemoveRigidBody();


		private:
			BulletCapsuleCollider collider_;
			BulletRigidBody       rigidBody_;

			math::Vector3 position_;
			math::Vector3 prevPosition_;

			float verticalVelocity_ = 0.0f;
			float gravity_          = 0.0f;
			float radius_           = 0.0f;
			float height_           = 0.0f;

			bool isInited_          = false;
			bool isRequestTeleport_ = false;
		};
	}
}
