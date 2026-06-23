#pragma once
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace aq
{
	namespace util
	{
		// 軽量 JSON 値クラス。UI ドキュメントローダー用途特化。
		class JsonValue
		{
		public:
			enum class Type { Null, Bool, Number, String, Array, Object };

			// ---- コンストラクタ ----
			JsonValue()                        : type_(Type::Null)   {}
			explicit JsonValue(bool v)         : type_(Type::Bool),   boolVal_(v) {}
			explicit JsonValue(double v)       : type_(Type::Number), numVal_(v)  {}
			explicit JsonValue(std::string v)  : type_(Type::String), strVal_(std::move(v)) {}

			static JsonValue MakeArray()  { JsonValue v; v.type_ = Type::Array;  return v; }
			static JsonValue MakeObject() { JsonValue v; v.type_ = Type::Object; return v; }

			// ---- 型判定 ----
			bool IsNull()   const { return type_ == Type::Null;   }
			bool IsBool()   const { return type_ == Type::Bool;   }
			bool IsNumber() const { return type_ == Type::Number; }
			bool IsString() const { return type_ == Type::String; }
			bool IsArray()  const { return type_ == Type::Array;  }
			bool IsObject() const { return type_ == Type::Object; }

			// ---- 値取得 ----
			bool               AsBool  (bool   def = false) const { return IsBool()   ? boolVal_ : def; }
			float              AsFloat (float  def = 0.f)   const { return IsNumber() ? (float)numVal_ : def; }
			int                AsInt   (int    def = 0)     const { return IsNumber() ? (int)numVal_   : def; }
			const std::string& AsString()                   const { return strVal_; }

			// ---- 配列 ----
			size_t Size() const { return arrVal_.size(); }
			const JsonValue& operator[](size_t i) const
			{
				return (i < arrVal_.size()) ? arrVal_[i] : Null();
			}
			void PushBack(JsonValue v) { arrVal_.push_back(std::move(v)); }
			const std::vector<JsonValue>& GetArray() const { return arrVal_; }

			// ---- オブジェクト ----
			bool Contains(std::string_view key) const
			{
				return objVal_.count(std::string(key)) > 0;
			}
			const JsonValue& operator[](std::string_view key) const
			{
				auto it = objVal_.find(std::string(key));
				return (it != objVal_.end()) ? it->second : Null();
			}
			void Set(std::string key, JsonValue v) { objVal_[std::move(key)] = std::move(v); }
			const std::unordered_map<std::string, JsonValue>& GetObject() const { return objVal_; }

			// ---- Deep merge (overrides パッチ用) ----
			// オブジェクト同士: 再帰マージ。その他: override で上書き。
			void Merge(const JsonValue& overrides);

			static const JsonValue& Null()
			{
				static JsonValue sNull;
				return sNull;
			}

		private:
			Type   type_    = Type::Null;
			bool   boolVal_ = false;
			double numVal_  = 0.0;
			std::string strVal_;
			std::vector<JsonValue>                    arrVal_;
			std::unordered_map<std::string, JsonValue> objVal_;
		};


		// 軽量 JSON パーサー
		class JsonParser
		{
		public:
			// テキストを JsonValue に変換。失敗時は Null を返す。
			static JsonValue ParseString(std::string_view text);

			// ファイルを読み込んで ParseString に渡す。
			static JsonValue ParseFile(const char* path);
		};

	} // namespace util
} // namespace aq
