#pragma once
#include "Util/SimpleJson.h"
#include "Math/Vector.h"
#include <string>

namespace aq
{
	namespace ecs
	{
		// コンポーネントの Reflect<V> を JSON へ書き出す Visitor（常時コンパイル）。
		// Field()    = 永続化対象（persistKey をキーに使う）
		// FieldPath()= パス文字列。書き出しでは副作用を起こさないため常に false を返す。
		// ReadOnly() = 派生/runtime 値。永続化しない（no-op）。
		struct JsonWriteVisitor
		{
			util::JsonValue obj = util::JsonValue::MakeObject();

			void Field(const char* key, float& v, const char* = nullptr)
			{
				obj.Set(key, util::JsonValue(static_cast<double>(v)));
			}

			void Field(const char* key, int& v, const char* = nullptr)
			{
				obj.Set(key, util::JsonValue(static_cast<double>(v)));
			}

			void Field(const char* key, bool& v, const char* = nullptr)
			{
				obj.Set(key, util::JsonValue(v));
			}

			void Field(const char* key, aq::math::Vector3& v, const char* = nullptr)
			{
				util::JsonValue a = util::JsonValue::MakeArray();
				a.PushBack(util::JsonValue(static_cast<double>(v.x)));
				a.PushBack(util::JsonValue(static_cast<double>(v.y)));
				a.PushBack(util::JsonValue(static_cast<double>(v.z)));
				obj.Set(key, std::move(a));
			}

			// Quaternion は [x,y,z,w] でロスレス保存する（Euler 変換は使わない）。
			void Field(const char* key, aq::math::Quaternion& q, const char* = nullptr)
			{
				util::JsonValue a = util::JsonValue::MakeArray();
				a.PushBack(util::JsonValue(static_cast<double>(q.x)));
				a.PushBack(util::JsonValue(static_cast<double>(q.y)));
				a.PushBack(util::JsonValue(static_cast<double>(q.z)));
				a.PushBack(util::JsonValue(static_cast<double>(q.w)));
				obj.Set(key, std::move(a));
			}

			bool FieldPath(const char* key, std::string& path, const char* = nullptr)
			{
				obj.Set(key, util::JsonValue(path));
				return false;
			}

			void ReadOnly(const char*, const aq::math::Vector3&)    {}
			void ReadOnly(const char*, const aq::math::Quaternion&) {}
			void ReadOnly(const char*, const char*)                 {}
			void ReadOnly(const char*, int)                         {}
		};


		// JSON からコンポーネントの Reflect<V> へ読み込む Visitor（常時コンパイル）。
		// キーが存在する場合のみ値を上書きする（存在しなければ現在値を保持）。
		// FieldPath() はキーが存在し値を更新したとき true を返す → 呼び出し側で
		// SetXxxPath などのロード副作用を発火させる（deserialize 時にリソースを読む）。
		struct JsonReadVisitor
		{
			const util::JsonValue& obj;

			explicit JsonReadVisitor(const util::JsonValue& source) : obj(source) {}

			void Field(const char* key, float& v, const char* = nullptr)
			{
				if (obj.Contains(key)) v = obj[key].AsFloat(v);
			}

			void Field(const char* key, int& v, const char* = nullptr)
			{
				if (obj.Contains(key)) v = obj[key].AsInt(v);
			}

			void Field(const char* key, bool& v, const char* = nullptr)
			{
				if (obj.Contains(key)) v = obj[key].AsBool(v);
			}

			void Field(const char* key, aq::math::Vector3& v, const char* = nullptr)
			{
				const util::JsonValue& a = obj[key];
				if (a.IsArray() && a.Size() >= 3)
				{
					v.x = a[0].AsFloat(v.x);
					v.y = a[1].AsFloat(v.y);
					v.z = a[2].AsFloat(v.z);
				}
			}

			void Field(const char* key, aq::math::Quaternion& q, const char* = nullptr)
			{
				const util::JsonValue& a = obj[key];
				if (a.IsArray() && a.Size() >= 4)
				{
					q.x = a[0].AsFloat(q.x);
					q.y = a[1].AsFloat(q.y);
					q.z = a[2].AsFloat(q.z);
					q.w = a[3].AsFloat(q.w);
				}
			}

			bool FieldPath(const char* key, std::string& path, const char* = nullptr)
			{
				if (!obj.Contains(key)) return false;
				path = obj[key].AsString();
				return true;
			}

			void ReadOnly(const char*, const aq::math::Vector3&)    {}
			void ReadOnly(const char*, const aq::math::Quaternion&) {}
			void ReadOnly(const char*, const char*)                 {}
			void ReadOnly(const char*, int)                         {}
		};
	}
}
