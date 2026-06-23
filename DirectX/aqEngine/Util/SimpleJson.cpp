#include "aq.h"
#include "SimpleJson.h"
#include <fstream>
#include <cstring>
#include <cctype>
#include <cstdlib>

namespace aq
{
	namespace util
	{
		// ---- JsonValue::Merge ---------------------------------------------------

		void JsonValue::Merge(const JsonValue& overrides)
		{
			if (IsObject() && overrides.IsObject())
			{
				for (const auto& [key, val] : overrides.GetObject())
				{
					auto it = objVal_.find(key);
					if (it != objVal_.end() && it->second.IsObject() && val.IsObject())
						it->second.Merge(val);
					else
						objVal_[key] = val;
				}
			}
			else
			{
				*this = overrides;
			}
		}


		// ---- 内部パーサー -------------------------------------------------------

		namespace
		{
			struct ParseCtx
			{
				const char* p;
				const char* end;
			};

			static void SkipWs(ParseCtx& c)
			{
				while (c.p < c.end)
				{
					if (std::isspace(static_cast<unsigned char>(*c.p)))
					{
						++c.p;
					}
					else if (c.p + 1 < c.end && c.p[0] == '/' && c.p[1] == '/')
					{
						c.p += 2;
						while (c.p < c.end && *c.p != '\n') ++c.p;
					}
					else
					{
						break;
					}
				}
			}

			static JsonValue ParseVal(ParseCtx& c);

			// 引用符で囲まれた文字列を読む。c.p は '"' を指している。
			static std::string ParseQuotedStr(ParseCtx& c)
			{
				++c.p; // '"'
				std::string result;
				while (c.p < c.end && *c.p != '"')
				{
					if (*c.p == '\\')
					{
						++c.p;
						if (c.p >= c.end) break;
						switch (*c.p)
						{
							case '"':  result += '"';  break;
							case '\\': result += '\\'; break;
							case '/':  result += '/';  break;
							case 'n':  result += '\n'; break;
							case 'r':  result += '\r'; break;
							case 't':  result += '\t'; break;
							default:   result += *c.p; break;
						}
					}
					else
					{
						result += *c.p;
					}
					++c.p;
				}
				if (c.p < c.end) ++c.p; // 閉じ '"'
				return result;
			}

			static JsonValue ParseNum(ParseCtx& c)
			{
				const char* start = c.p;
				if (c.p < c.end && (*c.p == '-' || *c.p == '+')) ++c.p;
				while (c.p < c.end && std::isdigit(static_cast<unsigned char>(*c.p))) ++c.p;
				if (c.p < c.end && *c.p == '.')
				{
					++c.p;
					while (c.p < c.end && std::isdigit(static_cast<unsigned char>(*c.p))) ++c.p;
				}
				if (c.p < c.end && (*c.p == 'e' || *c.p == 'E'))
				{
					++c.p;
					if (c.p < c.end && (*c.p == '+' || *c.p == '-')) ++c.p;
					while (c.p < c.end && std::isdigit(static_cast<unsigned char>(*c.p))) ++c.p;
				}
				char buf[64] = {};
				size_t len   = static_cast<size_t>(c.p - start);
				if (len >= sizeof(buf)) len = sizeof(buf) - 1;
				std::memcpy(buf, start, len);
				return JsonValue(std::strtod(buf, nullptr));
			}

			static JsonValue ParseArr(ParseCtx& c)
			{
				++c.p; // '['
				JsonValue arr = JsonValue::MakeArray();
				SkipWs(c);
				if (c.p < c.end && *c.p == ']') { ++c.p; return arr; }
				while (c.p < c.end)
				{
					SkipWs(c);
					arr.PushBack(ParseVal(c));
					SkipWs(c);
					if (c.p < c.end && *c.p == ',') { ++c.p; continue; }
					break;
				}
				if (c.p < c.end && *c.p == ']') ++c.p;
				return arr;
			}

			static JsonValue ParseObj(ParseCtx& c)
			{
				++c.p; // '{'
				JsonValue obj = JsonValue::MakeObject();
				SkipWs(c);
				if (c.p < c.end && *c.p == '}') { ++c.p; return obj; }
				while (c.p < c.end)
				{
					SkipWs(c);
					if (c.p >= c.end || *c.p != '"') break; // malformed
					std::string key = ParseQuotedStr(c);
					SkipWs(c);
					if (c.p < c.end && *c.p == ':') ++c.p;
					SkipWs(c);
					JsonValue val = ParseVal(c);
					obj.Set(std::move(key), std::move(val));
					SkipWs(c);
					if (c.p < c.end && *c.p == ',') { ++c.p; continue; }
					break;
				}
				if (c.p < c.end && *c.p == '}') ++c.p;
				return obj;
			}

			static JsonValue ParseVal(ParseCtx& c)
			{
				SkipWs(c);
				if (c.p >= c.end) return JsonValue();
				const char ch = *c.p;
				if (ch == '"')
					return JsonValue(ParseQuotedStr(c));
				if (ch == '{')
					return ParseObj(c);
				if (ch == '[')
					return ParseArr(c);
				if (ch == 't' && c.p + 3 < c.end) { c.p += 4; return JsonValue(true);  }
				if (ch == 'f' && c.p + 4 < c.end) { c.p += 5; return JsonValue(false); }
				if (ch == 'n' && c.p + 3 < c.end) { c.p += 4; return JsonValue();      }
				if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch)))
					return ParseNum(c);
				return JsonValue();
			}

		} // anonymous namespace


		// ---- JsonParser ---------------------------------------------------------

		JsonValue JsonParser::ParseString(std::string_view text)
		{
			ParseCtx c{ text.data(), text.data() + text.size() };
			return ParseVal(c);
		}

		JsonValue JsonParser::ParseFile(const char* path)
		{
			std::ifstream ifs(path, std::ios::binary | std::ios::ate);
			if (!ifs.is_open()) return JsonValue();
			const auto size = static_cast<std::streamsize>(ifs.tellg());
			ifs.seekg(0);
			std::string buf(static_cast<size_t>(size), '\0');
			ifs.read(buf.data(), size);
			return ParseString(buf);
		}

	} // namespace util
} // namespace aq
