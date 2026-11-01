#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Trauma
{
	class JsonValue
	{
	public:
		enum class Type
		{
			Null,
			Boolean,
			Number,
			String,
			Array,
			Object
		};

		using Array = std::vector<JsonValue>;
		using Object = std::vector<std::pair<std::string, JsonValue>>;

		JsonValue() = default;

		static JsonValue MakeBoolean(bool value);
		static JsonValue MakeNumber(double value);
		static JsonValue MakeString(std::string value);
		static JsonValue MakeArray();
		static JsonValue MakeObject();
		static JsonValue MakeNull();

		Type GetType() const;
		bool IsNull() const;
		bool IsBoolean() const;
		bool IsNumber() const;
		bool IsString() const;
		bool IsArray() const;
		bool IsObject() const;

		bool GetBoolean(bool fallback = false) const;
		double GetNumber(double fallback = 0.0) const;
		std::string GetString(std::string fallback = {}) const;

		const Array& Items() const;
		Array& Items();
		const Object& Fields() const;
		Object& Fields();

		const JsonValue* Find(std::string_view key) const;
		JsonValue* Find(std::string_view key);
		const JsonValue& operator[](std::string_view key) const;
		JsonValue& operator[](std::string_view key);

		void Set(std::string key, JsonValue value);
		bool Erase(std::string_view key);
		std::size_t Size() const;

		static JsonValue Parse(std::string_view text, std::string& error);
		std::string Serialize(int indent = 2) const;

	private:
		Type type = Type::Null;
		bool boolean = false;
		double number = 0.0;
		std::string text;
		Array array;
		Object object;
	};
}
