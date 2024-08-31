#pragma once
#include "Vector.h"

namespace engine
{
	namespace math
	{
		// TODO: 将来的に3x3なども追加予定
		/** 4x4行列 */
		class Matrix4x4
		{
		public:
			union
			{
				DirectX::XMFLOAT4X4 matrix;
				struct
				{
					float _11, _12, _13, _14;
					float _21, _22, _23, _24;
					float _31, _32, _33, _34;
					float _41, _42, _43, _44;
				};
				float m[4][4];
			};


		public:
			operator DirectX::XMMATRIX() const
			{
				return DirectX::XMLoadFloat4x4(&matrix);
			}
			Matrix4x4& operator=(const Matrix4x4& mat)
			{
				matrix = mat.matrix;
				return *this;
			}
			/** コンストラクタ */
			Matrix4x4() {}
			Matrix4x4(float m00, float m01, float m02, float m03,
					  float m10, float m11, float m12, float m13,
					  float m20, float m21, float m22, float m23,
					  float m30, float m31, float m32, float m33) :
					  matrix(m00, m01, m02, m03,
							 m10, m11, m12, m13,
							 m20, m21, m22, m23,
							 m30, m31, m32, m33)
			{
			}
			/** 平行移動行列作成 */
			void MakeTranslation(const Vector3& translation)
			{
				DirectX::XMStoreFloat4x4(&matrix, DirectX::XMMatrixTranslationFromVector(translation));
			}
			/** Y軸周りの回転行列作成 */
			void MakeRotationY(float angle)
			{
				DirectX::XMStoreFloat4x4(&matrix, DirectX::XMMatrixRotationY(angle));
			}
			/** Z軸周りの回転行列作成 */
			void MakeRotationZ(float angle)
			{
				DirectX::XMStoreFloat4x4(&matrix, DirectX::XMMatrixRotationZ(angle));
			}
			/** Y軸周りの回転行列作成 */
			void MakeRotationX(float angle)
			{
				DirectX::XMStoreFloat4x4(&matrix, DirectX::XMMatrixRotationX(angle));
			}
			/** クォータニオンから回転行列作成 */
			void MakeRotationFromQuaternion(const Quaternion& q)
			{
				DirectX::XMStoreFloat4x4(&matrix, DirectX::XMMatrixRotationQuaternion(q));
			}
			/** 任意の軸周りの回転行列作成 */
			void MakeRotationAxis(const Vector3& axis, float angle)
			{
				DirectX::XMStoreFloat4x4(&matrix, DirectX::XMMatrixRotationAxis(axis, angle));
			}
			/** 拡大行列作成 */
			void MakeScaling(const Vector3& scale)
			{
				DirectX::XMStoreFloat4x4(&matrix, DirectX::XMMatrixScalingFromVector(scale));
			}
			/** プロジェクション行列作成 */
			void MakeProjectionMatrix(float viewAngle, float aspect, float nearZ, float farZ)
			{
				DirectX::XMStoreFloat4x4(&matrix, DirectX::XMMatrixPerspectiveFovLH(viewAngle, aspect, nearZ, farZ));
			}
			/** カメラ行列作成 */
			void MakeLookAt(const Vector3& position, const Vector3& target, const Vector3& up)
			{
				DirectX::XMStoreFloat4x4(&matrix, DirectX::XMMatrixLookAtLH(position, target, up));
			}
			/** 行列同士の乗算 */
			void Mull(const Matrix4x4& m0, const Matrix4x4& m1)
			{
				DirectX::XMStoreFloat4x4(&matrix, DirectX::XMMatrixMultiply(m0, m1));
			}
			/** 逆行列計算 */
			void Inverse(const Matrix4x4& mat)
			{
				DirectX::XMStoreFloat4x4(&matrix, DirectX::XMMatrixInverse(nullptr, mat));
			}
			/** 行列を転置 */
			void Transpose()
			{
				DirectX::XMStoreFloat4x4(&matrix, DirectX::XMMatrixTranspose(*this));
			}


		public:
			/** 単位行列 */
			static const Matrix4x4 Identity;
		};
	}
}