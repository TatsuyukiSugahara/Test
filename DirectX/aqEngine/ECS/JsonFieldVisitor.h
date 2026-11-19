#pragma once
#include "Util/SimpleJson.h"
#include "Math/Vector.h"
#include "FieldAttr.h"
#include <string>

namespace aq
{
	namespace ecs
	{
		// 型ごとの JSON 表現（JsonWriteVisitor と編集イベント Visitor が共有する。設計 6.4 節 3）。
		// 数値は double、Vector3 は [x,y,z]、Quaternion は [x,y,z,w]（Euler 変換は使わない）。
		inline util::JsonValue ToJson(const float& v)
		{
			return util::JsonValue(static_cast<double>(v));
		}

		inline util::JsonValue ToJson(const int& v)
		{
			return util::JsonValue(static_cast<double>(v));
		}

		inline util::JsonValue ToJson(const bool& v)
		{
			return util::JsonValue(v);
		}

		inline util::JsonValue ToJson(const aq::math::Vector3& v)
		{
			util::JsonValue a = util::JsonValue::MakeArray();
			a.PushBack(util::JsonValue(static_cast<double>(v.x)));
			a.PushBack(util::JsonValue(static_cast<double>(v.y)));
			a.PushBack(util::JsonValue(static_cast<double>(v.z)));
			return a;
		}

		inline util::JsonValue ToJson(const aq::math::Quaternion& q)
		{
			util::JsonValue a = util::JsonValue::MakeArray();
			a.PushBack(util::JsonValue(static_cast<double>(q.x)));
			a.PushBack(util::JsonValue(static_cast<double>(q.y)));
			a.PushBack(util::JsonValue(static_cast<double>(q.z)));
			a.PushBack(util::JsonValue(static_cast<double>(q.w)));
			return a;
		}

		inline util::JsonValue ToJson(const std::string& v)
		{
			return util::JsonValue(v);
		}


		// コンポーネントの Reflect<V> を JSON へ書き出す Visitor（常時コンパイル）。
		// Field()    = 永続化対象（persistKey をキーに使う）。値の表現は上の ToJson。
		// FieldPath()= パス文字列。書き出しでは副作用を起こさないため常に false を返す。
		// ReadOnly() = 派生/runtime 値。永続化しない（no-op）。
		struct JsonWriteVisitor
		{
			util::JsonValue obj = util::JsonValue::MakeObject();

			// 末尾の attr（フィールド属性）は JSON では無視する。
			void Field(const char* key, float& v, const char* = nullptr, const FieldAttr& = {})
			{
				obj.Set(key, ToJson(v));
			}

			void Field(const char* key, int& v, const char* = nullptr, const FieldAttr& = {})
			{
				obj.Set(key, ToJson(v));
			}

			void Field(const char* key, bool& v, const char* = nullptr, const FieldAttr& = {})
			{
				obj.Set(key, ToJson(v));
			}

			void Field(const char* key, aq::math::Vector3& v, const char* = nullptr, const FieldAttr& = {})
			{
				obj.Set(key, ToJson(v));
			}

			// Quaternion は [x,y,z,w] でロスレス保存する（Euler 変換は使わない）。
			void Field(const char* key, aq::math::Quaternion& q, const char* = nullptr, const FieldAttr& = {})
			{
				obj.Set(key, ToJson(q));
			}

			// パス以外の文字列。
			void Field(const char* key, std::string& v, const char* = nullptr, const FieldAttr& = {})
			{
				obj.Set(key, ToJson(v));
			}

			bool FieldPath(const char* key, std::string& path, const char* = nullptr, const FieldAttr& = {})
			{
				obj.Set(key, ToJson(path));
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

			// 末尾の attr（フィールド属性）は JSON では無視する（最小・最大でも読み込んだ値は丸めない）。
			void Field(const char* key, float& v, const char* = nullptr, const FieldAttr& = {})
			{
				if (obj.Contains(key)) v = obj[key].AsFloat(v);
			}

			void Field(const char* key, int& v, const char* = nullptr, const FieldAttr& = {})
			{
				if (obj.Contains(key)) v = obj[key].AsInt(v);
			}

			void Field(const char* key, bool& v, const char* = nullptr, const FieldAttr& = {})
			{
				if (obj.Contains(key)) v = obj[key].AsBool(v);
			}

			void Field(const char* key, aq::math::Vector3& v, const char* = nullptr, const FieldAttr& = {})
			{
				const util::JsonValue& a = obj[key];
				if (a.IsArray() && a.Size() >= 3)
				{
					v.x = a[0].AsFloat(v.x);
					v.y = a[1].AsFloat(v.y);
					v.z = a[2].AsFloat(v.z);
				}
			}

			void Field(const char* key, aq::math::Quaternion& q, const char* = nullptr, const FieldAttr& = {})
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

			// パス以外の文字列。
			void Field(const char* key, std::string& v, const char* = nullptr, const FieldAttr& = {})
			{
				if (obj.Contains(key)) v = obj[key].AsString();
			}

			bool FieldPath(const char* key, std::string& path, const char* = nullptr, const FieldAttr& = {})
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
