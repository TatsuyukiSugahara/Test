#include "aq.h"
#include "Vector.h"


namespace aq
{
	namespace math
	{
		Vector3 Vector3::Zero = Vector3(0.0f, 0.0f, 0.0f);
		Vector3 Vector3::One = Vector3(1.0f, 1.0f, 1.0f);
		Vector3 Vector3::Up = Vector3(0.0f, 1.0f, 0.0f);

		Quaternion Quaternion::Identity = Quaternion(0.0f, 0.0f, 0.0f, 1.0f);

		void Quaternion::SetEuler(const aq::math::Vector3& rot)
		{
			aq::math::Vector3 rotation;
			rotation.x = aq::math::DegToRadian(rot.x);
			rotation.y = aq::math::DegToRadian(rot.y);
			rotation.z = aq::math::DegToRadian(rot.z);
			DirectX::XMVECTOR xv = DirectX::XMQuaternionRotationRollPitchYawFromVector(rotation);
			DirectX::XMStoreFloat4(&vector, xv);
		}
	}
}