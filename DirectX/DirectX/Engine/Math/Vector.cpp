#include "../EnginePreCompile.h"
#include "Vector.h"


namespace engine
{
	namespace math
	{
		Vector3 Vector3::Zero = Vector3(0.0f, 0.0f, 0.0f);
		Vector3 Vector3::One = Vector3(1.0f, 1.0f, 1.0f);
		Vector3 Vector3::Up = Vector3(0.0f, 1.0f, 0.0f);

		Quaternion Quaternion::Identity = Quaternion(0.0f, 0.0f, 0.0f, 1.0f);

		void Quaternion::SetEuler(const engine::math::Vector3& rot)
		{
			engine::math::Vector3 rotation;
			rotation.x = engine::math::DegToRadian(rot.x);
			rotation.y = engine::math::DegToRadian(rot.y);
			rotation.z = engine::math::DegToRadian(rot.z);
			DirectX::XMVECTOR xv = DirectX::XMQuaternionRotationRollPitchYawFromVector(rotation);
			DirectX::XMStoreFloat4(&vector, xv);
		}
	}
}