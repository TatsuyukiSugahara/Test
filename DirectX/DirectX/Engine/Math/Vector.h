/**
 * @brief ベクトル
 */
#pragma once

namespace engine
{
	namespace math
	{
		/**
		 * 2次元ベクトル
		 */
		class Vector2
		{
		public:
			union
			{
				struct { float x, y; };
				float a[2];
			};


		public:
			/** コンストラクタ */
			Vector2() {}
			Vector2(float x, float y) : x(x), y(y) {}
			Vector2(float xy) : x(xy), y(xy) {}

			/** 代入演算子 */
			Vector2& operator=(const Vector2& v)
			{
				x = v.x;
				y = v.y;
				return *this;
			}

			/** 一致するか */
			inline bool IsEquals(const Vector2& v, const float value = FLT_EPSILON) const
			{
				if (fabs(x - v.x) >= value) return false;
				if (fabs(y - v.y) >= value) return false;
				return true;
			}
			/** 各要素の設定 */
			inline void Set(float _x, float _y)
			{
				x = _x;
				y = _y;
			}
			inline void Set(const Vector2& v)
			{
				Set(v.x, v.y);
			}
		};




		/*******************************************/


		/**
		 * 3次元ベクトル
		 */
		class Vector3
		{
		public:
			static Vector3 Zero;
			static Vector3 One;
			static Vector3 Up;

		public:
			union
			{
				DirectX::XMFLOAT3 vector;
				float a[3];
				struct { float x, y, z; };
			};


		public:
			operator DirectX::XMVECTOR() const
			{
				return DirectX::XMLoadFloat3(&vector);
			}

			/** コンストラクタ */
			Vector3() {}
			Vector3(float x, float y, float z) : x(x), y(y), z(z) {}
			Vector3(float xyz) : x(xyz), y(xyz), z(xyz) {}

			/** 代入演算子 */
			Vector3& operator=(const Vector3& v)
			{
				vector = v.vector;
				return *this;
			}
			/** 加算 */
			Vector3 operator+(const Vector3& v)
			{
				Vector3 out;
				out.x = x + v.x;
				out.y = y + v.y;
				out.z = z + v.z;
				return out;
			}
			/** 減算 */
			Vector3 operator-(const Vector3& v)
			{
				Vector3 out;
				x = x - v.x;
				y = y - v.y;
				z = z - v.z;
				return out;
			}
			/** 乗算 */
			Vector3 operator*(const Vector3& v)
			{
				Vector3 out;
				x = x * v.x;
				y = y * v.y;
				z = z * v.z;
				return out;
			}
			/** 除算 */
			Vector3 operator/(const Vector3& v)
			{
				Vector3 out;
				x = x / v.x;
				y = y / v.y;
				z = z / v.z;
				return out;
			}
			/** 一致するか */
			inline bool IsEquals(const Vector3& v, const float value = FLT_EPSILON) const
			{
				if (fabs(x - v.x) >= value) return false;
				if (fabs(y - v.y) >= value) return false;
				if (fabs(z - v.z) >= value) return false;
				return true;
			}
			/** ベクトルの各要素設定 */
			inline void Set(float x, float y, float z)
			{
				vector.x = x;
				vector.y = y;
				vector.z = z;
			}
			inline void Set(float xyz)
			{
				vector.x = xyz;
				vector.y = xyz;
				vector.z = xyz;
			}
			inline void Set(const Vector3& v)
			{
				vector = v.vector;
			}

			/** ベクトルを加算 */
			inline void Add(const Vector3& v)
			{
				DirectX::XMVECTOR xv0 = DirectX::XMLoadFloat3(&vector);
				DirectX::XMVECTOR xv1 = DirectX::XMLoadFloat3(&v.vector);
				DirectX::XMVECTOR xvr = DirectX::XMVectorAdd(xv0, xv1);
				DirectX::XMStoreFloat3(&vector, xvr);
			}
			inline void Add(const Vector3& v0, const Vector3& v1)
			{
				DirectX::XMVECTOR xv0 = DirectX::XMLoadFloat3(&v0.vector);
				DirectX::XMVECTOR xv1 = DirectX::XMLoadFloat3(&v1.vector);
				DirectX::XMVECTOR xvr = DirectX::XMVectorAdd(xv0, xv1);
				DirectX::XMStoreFloat3(&vector, xvr);
			}
			/** ベクトルを減算 */
			inline void Substract(const Vector3& v)
			{
				DirectX::XMVECTOR xv0 = DirectX::XMLoadFloat3(&vector);
				DirectX::XMVECTOR xv1 = DirectX::XMLoadFloat3(&v.vector);
				DirectX::XMVECTOR xvr = DirectX::XMVectorSubtract(xv0, xv1);
				DirectX::XMStoreFloat3(&vector, xvr);
			}
			inline void Substract(const Vector3& v0, const Vector3& v1)
			{
				DirectX::XMVECTOR xv0 = DirectX::XMLoadFloat3(&v0.vector);
				DirectX::XMVECTOR xv1 = DirectX::XMLoadFloat3(&v1.vector);
				DirectX::XMVECTOR xvr = DirectX::XMVectorSubtract(xv0, xv1);
				DirectX::XMStoreFloat3(&vector, xvr);
			}
			/** 内積 */
			inline float Dot(const Vector3& v) const
			{
				DirectX::XMVECTOR xv0 = DirectX::XMLoadFloat3(&vector);
				DirectX::XMVECTOR xv1 = DirectX::XMLoadFloat3(&v.vector);
				return DirectX::XMVector3Dot(xv0, xv1).m128_f32[0];
			}
			/** 外積 */
			inline void Cross(const Vector3& v)
			{
				DirectX::XMVECTOR xv0 = DirectX::XMLoadFloat3(&vector);
				DirectX::XMVECTOR xv1 = DirectX::XMLoadFloat3(&v.vector);
				DirectX::XMVECTOR xvr = DirectX::XMVector3Cross(xv0, xv1);
				DirectX::XMStoreFloat3(&vector, xvr);
			}
			inline void Cross(const Vector3& v0, const Vector3& v1)
			{
				DirectX::XMVECTOR xv0 = DirectX::XMLoadFloat3(&v0.vector);
				DirectX::XMVECTOR xv1 = DirectX::XMLoadFloat3(&v1.vector);
				DirectX::XMVECTOR xvr = DirectX::XMVector3Cross(xv0, xv1);
				DirectX::XMStoreFloat3(&vector, xvr);
			}
			/** 長さを取得 */
			inline float Length() const
			{
				DirectX::XMVECTOR xv = DirectX::XMLoadFloat3(&vector);
				return DirectX::XMVector3Length(xv).m128_f32[0];
			}
			/** 長さの2乗を取得 */
			inline float LengthSq() const
			{
				DirectX::XMVECTOR xv = DirectX::XMLoadFloat3(&vector);
				return DirectX::XMVector3LengthSq(xv).m128_f32[0];
			}
			/** 拡大 */
			inline void Scale(float s)
			{
				DirectX::XMVECTOR xv = DirectX::XMLoadFloat3(&vector);
				xv = DirectX::XMVectorScale(xv, s);
				DirectX::XMStoreFloat3(&vector, xv);
			}
			/** 正規化 */
			inline void Normalize()
			{
				DirectX::XMVECTOR xv = DirectX::XMLoadFloat3(&vector);
				xv = DirectX::XMVector3Normalize(xv);
				DirectX::XMStoreFloat3(&vector, xv);
			}
			/** 除算 */
			inline void Div(float d)
			{
				float scale = 1.0f / d;
				Scale(d);
			}
		};




		/*******************************************/


		/**
		 * 4要素のベクトル
		 */
		class Vector4
		{
		public:
			union
			{
				DirectX::XMFLOAT4 vector;
				struct { float x,y,z,w; };
				float a[4];
			};


		public:
			operator DirectX::XMVECTOR() const
			{
				return DirectX::XMLoadFloat4(&vector);
			}
			Vector4& operator=(const Vector4& v)
			{
				vector = v.vector;
				return *this;
			}
			/**	コンストラクタ */
			Vector4() {}
			Vector4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
			Vector4(float xyzw) : x(xyzw), y(xyzw), z(xyzw), w(xyzw) {}
			Vector4(const Vector4& v) : x(v.x), y(v.y), z(v.z), w(v.w) {}
			/** 設定 */
			void Set(float x, float y, float z, float w)
			{
				this->x = x;
				this->y = y;
				this->z = z;
				this->w = w;
			}
			void Set(float xyzw)
			{
				this->x = xyzw;
				this->y = xyzw;
				this->z = xyzw;
				this->w = xyzw;
			}
			void Set(const Vector4& v)
			{
				*this = v;
			}
			/** 正規化 */
			void Normalize()
			{
				DirectX::XMVECTOR xv = DirectX::XMLoadFloat4(&vector);
				xv = DirectX::XMVector4Normalize(xv);
				DirectX::XMStoreFloat4(&vector, xv);
			}
			/** ベクトルを加算 */
			inline void Add(const Vector4& v)
			{
				DirectX::XMVECTOR xv0 = DirectX::XMLoadFloat4(&vector);
				DirectX::XMVECTOR xv1 = DirectX::XMLoadFloat4(&v.vector);
				DirectX::XMVECTOR xvr = DirectX::XMVectorAdd(xv0, xv1);
				DirectX::XMStoreFloat4(&vector, xvr);
			}
			inline void Add(const Vector4& v0, const Vector4& v1)
			{
				DirectX::XMVECTOR xv0 = DirectX::XMLoadFloat4(&v0.vector);
				DirectX::XMVECTOR xv1 = DirectX::XMLoadFloat4(&v1.vector);
				DirectX::XMVECTOR xvr = DirectX::XMVectorAdd(xv0, xv1);
				DirectX::XMStoreFloat4(&vector, xvr);
			}
			/** ベクトルを減算 */
			inline void Substract(const Vector4& v)
			{
				DirectX::XMVECTOR xv0 = DirectX::XMLoadFloat4(&vector);
				DirectX::XMVECTOR xv1 = DirectX::XMLoadFloat4(&v.vector);
				DirectX::XMVECTOR xvr = DirectX::XMVectorSubtract(xv0, xv1);
				DirectX::XMStoreFloat4(&vector, xvr);
			}
			inline void Substract(const Vector4& v0, const Vector4& v1)
			{
				DirectX::XMVECTOR xv0 = DirectX::XMLoadFloat4(&v0.vector);
				DirectX::XMVECTOR xv1 = DirectX::XMLoadFloat4(&v1.vector);
				DirectX::XMVECTOR xvr = DirectX::XMVectorSubtract(xv0, xv1);
				DirectX::XMStoreFloat4(&vector, xvr);
			}
			/** 内積 */
			inline float Dot(const Vector4& v) const
			{
				DirectX::XMVECTOR xv0 = DirectX::XMLoadFloat4(&vector);
				DirectX::XMVECTOR xv1 = DirectX::XMLoadFloat4(&v.vector);
				return DirectX::XMVector4Dot(xv0, xv1).m128_f32[0];
			}
			/** 長さを取得 */
			inline float Length()
			{
				DirectX::XMVECTOR xv = DirectX::XMLoadFloat4(&vector);
				return DirectX::XMVector4Length(xv).m128_f32[0];
			}
			/** 長さの2乗を取得 */
			inline float LengthSq()
			{
				DirectX::XMVECTOR xv = DirectX::XMLoadFloat4(&vector);
				return DirectX::XMVector4LengthSq(xv).m128_f32[0];
			}
			/** 拡大 */
			inline void Scale(float scale)
			{
				DirectX::XMVECTOR xv = DirectX::XMLoadFloat4(&vector);
				xv = DirectX::XMVectorScale(xv, scale);
				DirectX::XMStoreFloat4(&vector, xv);
			}
		};




		/*******************************************/


		class Quaternion : public Vector4
		{
		public:
			static Quaternion Identity;


		public:
			Quaternion() {}
			Quaternion(float x, float y, float z, float w) : Vector4(x, y, z, w) {}

			/** 任意の軸周りの回転クォータニオンを作成 */
			void SetRotation(const Vector3& axis, float angle)
			{
				float s;
				float halfAngle = angle * 0.5f;
				s = sinf(halfAngle);
				w = cosf(halfAngle);
				x = axis.x * s;
				y = axis.y * s;
				y = axis.z * s;
			}

			void SetEuler(const engine::math::Vector3& rot);
		};




		/*******************************************/


		namespace internal
		{
			template <typename T>
			class Vector2Template
			{
			public:
				union
				{
					struct { T x, y; };
					T a[2];
				};


			public:
				/** コンストラクタ */
				Vector2Template() {}
				Vector2Template(T x, T y) : x(x), y(y) {}
				Vector2Template(T xy) : x(xy), y(xy) {}

				/** 各要素の設定 */
				inline void Set(T _x, T _y)
				{
					x = _x;
					y = _y;
				}
			};
		}




		/*******************************************/

		using Vector2Int32 = internal::Vector2Template<int32_t>;
	}
}