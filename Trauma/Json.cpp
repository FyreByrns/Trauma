#include "Json.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <sstream>

namespace Trauma {
	namespace {
		class Parser {
		public:
			Parser(std::string_view text, std::string& error)
				: text(text), error(error) {}

			bool Parse(JsonValue& value) {
				SkipWhitespace();
				if (!ParseValue(value, 0))
					return false;

				SkipWhitespace();
				if (position != text.size())
					return Fail("trailing characters");

				return true;
			}

		private:
			bool Fail(std::string message) {
				if (error.empty())
					error = message + " at " + std::to_string(position);
				return false;
			}

			void SkipWhitespace() {
				while (position < text.size()) {
					const char character = text[position];
					if (character != ' ' && character != '\t' && character != '\n' && character != '\r')
						break;
					++position;
				}
			}

			bool ParseValue(JsonValue& value, int depth) {
				if (depth > 64)
					return Fail("nesting too deep");

				if (position >= text.size())
					return Fail("unexpected end of input");

				const char character = text[position];
				if (character == '{')
					return ParseObject(value, depth);
				if (character == '[')
					return ParseArray(value, depth);
				if (character == '"') {
					std::string parsed;
					if (!ParseString(parsed))
						return false;
					value = JsonValue::MakeString(std::move(parsed));
					return true;
				}
				if (character == 't' || character == 'f')
					return ParseBoolean(value);
				if (character == 'n')
					return ParseNull(value);
				if (character == '-' || (character >= '0' && character <= '9'))
					return ParseNumber(value);

				return Fail("unexpected character");
			}

			bool ParseObject(JsonValue& value, int depth) {
				++position;
				value = JsonValue::MakeObject();
				SkipWhitespace();

				if (position < text.size() && text[position] == '}') {
					++position;
					return true;
				}

				while (true) {
					SkipWhitespace();
					if (position >= text.size() || text[position] != '"')
						return Fail("expected object key");

					std::string key;
					if (!ParseString(key))
						return false;

					SkipWhitespace();
					if (position >= text.size() || text[position] != ':')
						return Fail("expected ':'");

					++position;
					SkipWhitespace();

					JsonValue item;
					if (!ParseValue(item, depth + 1))
						return false;

					value.Fields().emplace_back(std::move(key), std::move(item));
					SkipWhitespace();

					if (position >= text.size())
						return Fail("unterminated object");

					if (text[position] == ',') {
						++position;
						continue;
					}

					if (text[position] == '}') {
						++position;
						return true;
					}

					return Fail("expected ',' or '}'");
				}
			}

			bool ParseArray(JsonValue& value, int depth) {
				++position;
				value = JsonValue::MakeArray();
				SkipWhitespace();

				if (position < text.size() && text[position] == ']') {
					++position;
					return true;
				}

				while (true) {
					JsonValue item;
					if (!ParseValue(item, depth + 1))
						return false;

					value.Items().push_back(std::move(item));
					SkipWhitespace();

					if (position >= text.size())
						return Fail("unterminated array");

					if (text[position] == ',') {
						++position;
						SkipWhitespace();
						continue;
					}

					if (text[position] == ']') {
						++position;
						return true;
					}

					return Fail("expected ',' or ']'");
				}
			}

			bool ParseBoolean(JsonValue& value) {
				if (text.substr(position, 4) == "true") {
					position += 4;
					value = JsonValue::MakeBoolean(true);
					return true;
				}

				if (text.substr(position, 5) == "false") {
					position += 5;
					value = JsonValue::MakeBoolean(false);
					return true;
				}

				return Fail("invalid boolean");
			}

			bool ParseNull(JsonValue& value) {
				if (text.substr(position, 4) != "null")
					return Fail("invalid literal");

				position += 4;
				value = JsonValue::MakeNull();
				return true;
			}

			bool ParseNumber(JsonValue& value) {
				const size_t start = position;
				while (position < text.size()) {
					const char character = text[position];
					if ((character >= '0' && character <= '9')
						|| character == '-'
						|| character == '+'
						|| character == '.'
						|| character == 'e'
						|| character == 'E') {
						++position;
						continue;
					}

					break;
				}

				const std::string token(text.substr(start, position - start));
				char* end = nullptr;
				const double number = std::strtod(token.c_str(), &end);
				if (end == token.c_str() || *end != '\0' || !std::isfinite(number)) {
					position = start;
					return Fail("invalid number");
				}

				value = JsonValue::MakeNumber(number);
				return true;
			}

			bool ParseString(std::string& value) {
				++position;
				value.clear();

				while (position < text.size()) {
					const unsigned char character = static_cast<unsigned char>(text[position++]);
					if (character == '"')
						return true;

					if (character != '\\') {
						value.push_back(static_cast<char>(character));
						continue;
					}

					if (position >= text.size())
						return Fail("unterminated escape");

					const char escape = text[position++];
					switch (escape) {
						case '"':
							value.push_back('"');
							break;
						case '\\':
							value.push_back('\\');
							break;
						case '/':
							value.push_back('/');
							break;
						case 'b':
							value.push_back('\b');
							break;
						case 'f':
							value.push_back('\f');
							break;
						case 'n':
							value.push_back('\n');
							break;
						case 'r':
							value.push_back('\r');
							break;
						case 't':
							value.push_back('\t');
							break;
						case 'u':
						{
							unsigned code = 0;
							if (!ParseHex(code))
								return false;
							AppendUTF8(value, code);
							break;
						}
						default:
							return Fail("invalid escape");
					}
				}

				return Fail("unterminated string");
			}

			bool ParseHex(unsigned& value) {
				if (position + 4 > text.size())
					return Fail("invalid unicode escape");

				value = 0;
				for (int index = 0; index < 4; ++index) {
					const char character = text[position++];
					value <<= 4;

					if (character >= '0' && character <= '9')
						value |= static_cast<unsigned>(character - '0');
					else if (character >= 'a' && character <= 'f')
						value |= static_cast<unsigned>(character - 'a' + 10);
					else if (character >= 'A' && character <= 'F')
						value |= static_cast<unsigned>(character - 'A' + 10);
					else
						return Fail("invalid unicode escape");
				}

				return true;
			}

			static void AppendUTF8(std::string& value, unsigned code) {
				if (code <= 0x7F) {
					value.push_back(static_cast<char>(code));
				}
				else if (code <= 0x7FF) {
					value.push_back(static_cast<char>(0xC0 | (code >> 6)));
					value.push_back(static_cast<char>(0x80 | (code & 0x3F)));
				}
				else {
					value.push_back(static_cast<char>(0xE0 | (code >> 12)));
					value.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
					value.push_back(static_cast<char>(0x80 | (code & 0x3F)));
				}
			}

			std::string_view text;
			std::string& error;
			size_t position = 0;
		};

		class Serializer {
		public:
			Serializer(int indent)
				: indent(std::max(0, indent)) {}

			std::string Serialize(const JsonValue& value) {
				Write(value, 0);
				return stream.str();
			}

		private:
			void NewLine(int level) {
				stream << '\n' << std::string(static_cast<size_t>(indent * level), ' ');
			}

			void WriteEscaped(std::string_view value) {
				stream << '"';
				for (const unsigned char character : value) {
					switch (character) {
						case '"':
							stream << "\\\"";
							break;
						case '\\':
							stream << "\\\\";
							break;
						case '\b':
							stream << "\\b";
							break;
						case '\f':
							stream << "\\f";
							break;
						case '\n':
							stream << "\\n";
							break;
						case '\r':
							stream << "\\r";
							break;
						case '\t':
							stream << "\\t";
							break;
						default:
							if (character < 0x20) {
								stream << "\\u" << std::hex << std::setw(4) << std::setfill('0')
									<< static_cast<unsigned>(character) << std::dec << std::setfill(' ');
							}
							else {
								stream << static_cast<char>(character);
							}
							break;
					}
				}
				stream << '"';
			}

			void Write(const JsonValue& value, int level) {
				switch (value.GetType()) {
					case JsonValue::Type::Null:
						stream << "null";
						break;
					case JsonValue::Type::Boolean:
						stream << (value.GetBoolean() ? "true" : "false");
						break;
					case JsonValue::Type::Number:
						stream << std::setprecision(9) << value.GetNumber();
						break;
					case JsonValue::Type::String:
						WriteEscaped(value.GetString());
						break;
					case JsonValue::Type::Array:
					{
						const auto& items = value.Items();
						if (items.empty()) {
							stream << "[]";
							break;
						}

						stream << '[';
						for (size_t index = 0; index < items.size(); ++index) {
							if (index > 0)
								stream << ',';
							NewLine(level + 1);
							Write(items[index], level + 1);
						}
						NewLine(level);
						stream << ']';
						break;
					}
					case JsonValue::Type::Object:
					{
						const auto& fields = value.Fields();
						if (fields.empty()) {
							stream << "{}";
							break;
						}

						stream << '{';
						for (size_t index = 0; index < fields.size(); ++index) {
							if (index > 0)
								stream << ',';
							NewLine(level + 1);
							WriteEscaped(fields[index].first);
							stream << ": ";
							Write(fields[index].second, level + 1);
						}
						NewLine(level);
						stream << '}';
						break;
					}
				}
			}

			int indent = 2;
			std::ostringstream stream;
		};
	}

	JsonValue JsonValue::MakeBoolean(bool value) {
		JsonValue result;
		result.type = Type::Boolean;
		result.boolean = value;
		return result;
	}

	JsonValue JsonValue::MakeNumber(double value) {
		JsonValue result;
		result.type = Type::Number;
		result.number = value;
		return result;
	}

	JsonValue JsonValue::MakeString(std::string value) {
		JsonValue result;
		result.type = Type::String;
		result.text = std::move(value);
		return result;
	}

	JsonValue JsonValue::MakeArray() {
		JsonValue result;
		result.type = Type::Array;
		return result;
	}

	JsonValue JsonValue::MakeObject() {
		JsonValue result;
		result.type = Type::Object;
		return result;
	}

	JsonValue JsonValue::MakeNull() {
		return JsonValue{};
	}

	JsonValue::Type JsonValue::GetType() const {
		return type;
	}

	bool JsonValue::IsNull() const {
		return type == Type::Null;
	}

	bool JsonValue::IsBoolean() const {
		return type == Type::Boolean;
	}

	bool JsonValue::IsNumber() const {
		return type == Type::Number;
	}

	bool JsonValue::IsString() const {
		return type == Type::String;
	}

	bool JsonValue::IsArray() const {
		return type == Type::Array;
	}

	bool JsonValue::IsObject() const {
		return type == Type::Object;
	}

	bool JsonValue::GetBoolean(bool fallback) const {
		return type == Type::Boolean ? boolean : fallback;
	}

	double JsonValue::GetNumber(double fallback) const {
		return type == Type::Number ? number : fallback;
	}

	std::string JsonValue::GetString(std::string fallback) const {
		return type == Type::String ? text : std::move(fallback);
	}

	const JsonValue::Array& JsonValue::Items() const {
		return array;
	}

	JsonValue::Array& JsonValue::Items() {
		return array;
	}

	const JsonValue::Object& JsonValue::Fields() const {
		return object;
	}

	JsonValue::Object& JsonValue::Fields() {
		return object;
	}

	const JsonValue* JsonValue::Find(std::string_view key) const {
		if (type != Type::Object)
			return nullptr;

		for (const auto& field : object)
			if (field.first == key)
				return &field.second;

		return nullptr;
	}

	JsonValue* JsonValue::Find(std::string_view key) {
		if (type != Type::Object)
			return nullptr;

		for (auto& field : object)
			if (field.first == key)
				return &field.second;

		return nullptr;
	}

	const JsonValue& JsonValue::operator[](std::string_view key) const {
		static const JsonValue nullValue;
		const JsonValue* value = Find(key);
		return value ? *value : nullValue;
	}

	JsonValue& JsonValue::operator[](std::string_view key) {
		if (type != Type::Object)
			*this = MakeObject();

		for (auto& field : object)
			if (field.first == key)
				return field.second;

		object.emplace_back(std::string(key), JsonValue{});
		return object.back().second;
	}

	void JsonValue::Set(std::string key, JsonValue value) {
		(*this)[key] = std::move(value);
	}

	bool JsonValue::Erase(std::string_view key) {
		if (type != Type::Object)
			return false;

		const auto found = std::find_if(
			object.begin(),
			object.end(),
			[key](const auto& field) { return field.first == key; });

		if (found == object.end())
			return false;

		object.erase(found);
		return true;
	}

	std::size_t JsonValue::Size() const {
		if (type == Type::Array)
			return array.size();
		if (type == Type::Object)
			return object.size();
		return 0;
	}

	JsonValue JsonValue::Parse(std::string_view text, std::string& error) {
		error.clear();
		JsonValue value;
		Parser parser(text, error);
		if (!parser.Parse(value))
			return JsonValue{};

		return value;
	}

	std::string JsonValue::Serialize(int indent) const {
		return Serializer(indent).Serialize(*this);
	}
}
