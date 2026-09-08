#pragma once
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace aq
{
	namespace util
	{
		// 軽量 JSON 値クラス。UI ドキュメントローダー用途特化。
		//
		// 配列/オブジェクトのコンテナは unique_ptr で遅延確保する。以前は全ノードが vector と
		// unordered_map を実体で持っていたため、スカラー 1 個の生成・移動ごとに複数回のヒープ確保
		// (MSVC の unordered_map は既定構築でも確保する)が走り、Debug では MemoryTracker 経由の
		// 確保が支配的で 22KB の JSON 解析に 300〜700ms かかっていた(startup_timing.log 実測)。
		class JsonValue
		{
		public:
			enum class Type { Null, Bool, Number, String, Array, Object };
			using Array  = std::vector<JsonValue>;
			using Object = std::unordered_map<std::string, JsonValue>;

			// ---- コンストラクタ / コピー / ムーブ ----
			JsonValue()                        : type_(Type::Null)   {}
			explicit JsonValue(bool v)         : type_(Type::Bool),   boolVal_(v) {}
			explicit JsonValue(double v)       : type_(Type::Number), numVal_(v)  {}
			explicit JsonValue(std::string v)  : type_(Type::String), strVal_(std::move(v)) {}

			JsonValue(const JsonValue& other)
				: type_(other.type_), boolVal_(other.boolVal_), numVal_(other.numVal_), strVal_(other.strVal_)
				, arrVal_(other.arrVal_ ? std::make_unique<Array>(*other.arrVal_) : nullptr)
				, objVal_(other.objVal_ ? std::make_unique<Object>(*other.objVal_) : nullptr)
			{
			}
			JsonValue& operator=(const JsonValue& other)
			{
				if (this != &other) { JsonValue tmp(other); *this = std::move(tmp); }
				return *this;
			}
			JsonValue(JsonValue&&) noexcept            = default;
			JsonValue& operator=(JsonValue&&) noexcept = default;
			~JsonValue()                               = default;

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
			size_t Size() const { return arrVal_ ? arrVal_->size() : 0; }
			const JsonValue& operator[](size_t i) const
			{
				return (arrVal_ && i < arrVal_->size()) ? (*arrVal_)[i] : Null();
			}
			void PushBack(JsonValue v) { EnsureArray().push_back(std::move(v)); }
			const Array& GetArray() const { return arrVal_ ? *arrVal_ : EmptyArray(); }

			// ---- オブジェクト ----
			bool Contains(std::string_view key) const
			{
				return objVal_ && objVal_->count(std::string(key)) > 0;
			}
			const JsonValue& operator[](std::string_view key) const
			{
				if (!objVal_) return Null();
				auto it = objVal_->find(std::string(key));
				return (it != objVal_->end()) ? it->second : Null();
			}
			void Set(std::string key, JsonValue v) { EnsureObject()[std::move(key)] = std::move(v); }
			const Object& GetObject() const { return objVal_ ? *objVal_ : EmptyObject(); }

			// ---- Deep merge (overrides パッチ用) ----
			// オブジェクト同士: 再帰マージ。その他: override で上書き。
			void Merge(const JsonValue& overrides);

			static const JsonValue& Null()
			{
				static JsonValue sNull;
				return sNull;
			}

		private:
			Array&  EnsureArray()  { if (!arrVal_) arrVal_ = std::make_unique<Array>();  return *arrVal_; }
			Object& EnsureObject() { if (!objVal_) objVal_ = std::make_unique<Object>(); return *objVal_; }
			static const Array&  EmptyArray()  { static const Array  s; return s; }
			static const Object& EmptyObject() { static const Object s; return s; }

			Type   type_    = Type::Null;
			bool   boolVal_ = false;
			double numVal_  = 0.0;
			std::string strVal_;
			std::unique_ptr<Array>  arrVal_;   // Array のときだけ確保(空配列は null のまま)
			std::unique_ptr<Object> objVal_;   // Object のときだけ確保(空オブジェクトは null のまま)
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


		// JsonValue を JSON 文字列に変換するシリアライザー
		class JsonSerializer
		{
		public:
			// JsonValue を整形済み JSON 文字列へ変換する。
			static std::string Stringify(const JsonValue& v, int indent = 0);

			// JSON 文字列をファイルへ書き込む。成功時 true。
			static bool WriteFile(const char* path, const JsonValue& v);
		};

	} // namespace util
} // namespace aq
