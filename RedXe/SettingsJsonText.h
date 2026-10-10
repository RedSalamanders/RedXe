#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// A forward scanner over settings source text in the user document's dialect: JSON with comments and trailing commas,
// as yyjson reads it with YYJSON_READ_ALLOW_COMMENTS | YYJSON_READ_ALLOW_TRAILING_COMMAS. The diagnostic locator
// (SettingsV4.cpp) and the dock source patches (Settings.cpp) share it, so both read a document where the parser does.
// `line` and `column` are 1-based; a line ends at LF.
struct JsonTextCursor final
{
    std::string_view json;
    size_t index = 0;
    uint32_t line = 1;
    uint32_t column = 1;

    void SkipSpaceAndComments() noexcept
    {
        for (;;)
        {
            while (index < json.size())
            {
                const char ch = json[index];
                if (ch == ' ' || ch == '\t' || ch == '\r')
                {
                    ++index;
                    ++column;
                    continue;
                }
                if (ch == '\n')
                {
                    ++index;
                    ++line;
                    column = 1;
                    continue;
                }
                break;
            }
            if (index + 1 < json.size() && json[index] == '/' && json[index + 1] == '/')
            {
                // A line comment ends at CR or LF, exactly where the parser ends it.
                index += 2;
                column += 2;
                while (index < json.size() && json[index] != '\n' && json[index] != '\r')
                {
                    ++index;
                    ++column;
                }
                continue;
            }
            if (index + 1 < json.size() && json[index] == '/' && json[index + 1] == '*')
            {
                index += 2;
                column += 2;
                while (index + 1 < json.size() && !(json[index] == '*' && json[index + 1] == '/'))
                {
                    if (json[index] == '\n')
                    {
                        ++line;
                        column = 1;
                    }
                    else
                    {
                        ++column;
                    }
                    ++index;
                }
                if (index + 1 < json.size())
                {
                    index += 2;
                    column += 2;
                }
                continue;
            }
            break;
        }
    }

    void Advance() noexcept
    {
        if (index >= json.size())
        {
            return;
        }
        if (json[index] == '\n')
        {
            ++line;
            column = 1;
        }
        else
        {
            ++column;
        }
        ++index;
    }

    [[nodiscard]] bool Consume(char expected) noexcept
    {
        SkipSpaceAndComments();
        if (index >= json.size() || json[index] != expected)
        {
            return false;
        }
        Advance();
        return true;
    }

    [[nodiscard]] bool SkipString() noexcept
    {
        SkipSpaceAndComments();
        if (index >= json.size() || json[index] != '"')
        {
            return false;
        }
        Advance();
        while (index < json.size())
        {
            const char ch = json[index];
            if (ch == '"')
            {
                Advance();
                return true;
            }
            if (ch == '\\')
            {
                Advance();
                if (index < json.size())
                {
                    Advance();
                }
                continue;
            }
            Advance();
        }
        return false;
    }

    [[nodiscard]] bool ReadString(std::string& value) noexcept
    {
        SkipSpaceAndComments();
        if (index >= json.size() || json[index] != '"')
        {
            return false;
        }
        Advance();
        value.clear();
        while (index < json.size())
        {
            const char ch = json[index];
            if (ch == '"')
            {
                Advance();
                return true;
            }
            if (ch == '\\')
            {
                Advance();
                if (index < json.size())
                {
                    value.push_back(json[index]);
                    Advance();
                }
                continue;
            }
            value.push_back(ch);
            Advance();
        }
        return false;
    }

    [[nodiscard]] bool SkipValue() noexcept
    {
        SkipSpaceAndComments();
        if (index >= json.size())
        {
            return false;
        }
        const char ch = json[index];
        if (ch == '"')
        {
            return SkipString();
        }
        if (ch == '{')
        {
            Advance();
            SkipSpaceAndComments();
            if (index < json.size() && json[index] == '}')
            {
                Advance();
                return true;
            }
            for (;;)
            {
                if (!SkipString() || !Consume(':') || !SkipValue())
                {
                    return false;
                }
                SkipSpaceAndComments();
                if (index < json.size() && json[index] == ',')
                {
                    Advance();
                    SkipSpaceAndComments();
                    if (index < json.size() && json[index] == '}')
                    {
                        Advance();
                        return true;
                    }
                    continue;
                }
                return Consume('}');
            }
        }
        if (ch == '[')
        {
            Advance();
            SkipSpaceAndComments();
            if (index < json.size() && json[index] == ']')
            {
                Advance();
                return true;
            }
            for (;;)
            {
                if (!SkipValue())
                {
                    return false;
                }
                SkipSpaceAndComments();
                if (index < json.size() && json[index] == ',')
                {
                    Advance();
                    SkipSpaceAndComments();
                    if (index < json.size() && json[index] == ']')
                    {
                        Advance();
                        return true;
                    }
                    continue;
                }
                return Consume(']');
            }
        }
        if (ch == '-' || (ch >= '0' && ch <= '9'))
        {
            while (index < json.size())
            {
                const char digit = json[index];
                if ((digit >= '0' && digit <= '9') || digit == '-' || digit == '+' || digit == '.' || digit == 'e' ||
                    digit == 'E')
                {
                    Advance();
                    continue;
                }
                break;
            }
            return true;
        }
        static constexpr std::string_view kTrue = "true";
        static constexpr std::string_view kFalse = "false";
        static constexpr std::string_view kNull = "null";
        const std::string_view word = json.substr(index);
        if (word.starts_with(kTrue))
        {
            for (size_t n = 0; n < kTrue.size(); ++n)
            {
                Advance();
            }
            return true;
        }
        if (word.starts_with(kFalse))
        {
            for (size_t n = 0; n < kFalse.size(); ++n)
            {
                Advance();
            }
            return true;
        }
        if (word.starts_with(kNull))
        {
            for (size_t n = 0; n < kNull.size(); ++n)
            {
                Advance();
            }
            return true;
        }
        return false;
    }
};
