#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <yyjson.h>

inline constexpr yyjson_read_flag kSettingsJsonReadFlags = YYJSON_READ_JSON5 | YYJSON_READ_ALLOW_BOM;

// Locates tokens in yyjson-validated settings source without changing the authored bytes.
// Diagnostic lines and byte columns are 1-based and advance to a new line at LF.
struct JsonTextCursor final
{
    std::string_view json;
    size_t index    = 0;
    uint32_t line   = 1;
    uint32_t column = 1;

    [[nodiscard]] static bool IsQuote(char character) noexcept
    {
        return character == '"' || character == '\'';
    }

    [[nodiscard]] static size_t LineBreakSize(std::string_view text) noexcept
    {
        if (text.starts_with("\r\n"))
        {
            return 2;
        }
        if (text.starts_with('\r') || text.starts_with('\n'))
        {
            return 1;
        }
        return text.starts_with("\xE2\x80\xA8") || text.starts_with("\xE2\x80\xA9") ? 3 : 0;
    }

    [[nodiscard]] static size_t WhitespaceSize(std::string_view text) noexcept
    {
        if (text.empty())
        {
            return 0;
        }
        if (std::string_view(" \t\r\n\v\f").find(text.front()) != std::string_view::npos)
        {
            return 1;
        }
        if (text.starts_with("\xC2\xA0"))
        {
            return 2;
        }
        // Match the UTF-8 whitespace accepted by yyjson's extended reader.
        if (text.size() >= 3 && text.starts_with("\xE2\x80"))
        {
            const auto third = static_cast<unsigned char>(text[2]);
            if ((third >= 0x80 && third <= 0x8A) || third == 0xA8 || third == 0xA9 || third == 0xAF)
            {
                return 3;
            }
        }
        return text.starts_with("\xE1\x9A\x80") || text.starts_with("\xE2\x81\x9F") || text.starts_with("\xE3\x80\x80") || text.starts_with("\xEF\xBB\xBF") ? 3
                                                                                                                                                            : 0;
    }

    void Advance() noexcept
    {
        if (index >= json.size())
        {
            return;
        }
        if (json[index++] == '\n')
        {
            ++line;
            column = 1;
        }
        else
        {
            ++column;
        }
    }

    void SkipSpaceAndComments() noexcept
    {
        while (index < json.size())
        {
            const auto remaining = json.substr(index);
            if (const size_t space = WhitespaceSize(remaining); space != 0)
            {
                const size_t end = index + space;
                while (index < end)
                {
                    Advance();
                }
            }
            else if (remaining.starts_with("//"))
            {
                while (index < json.size() && LineBreakSize(json.substr(index)) == 0)
                {
                    Advance();
                }
            }
            else if (remaining.starts_with("/*"))
            {
                const size_t closing = json.find("*/", index + 2);
                const size_t end     = closing == std::string_view::npos ? json.size() : closing + 2;
                while (index < end)
                {
                    Advance();
                }
            }
            else
            {
                return;
            }
        }
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
        if (index >= json.size() || ! IsQuote(json[index]))
        {
            return false;
        }
        const char quote = json[index];
        Advance();
        while (index < json.size())
        {
            const char character = json[index];
            Advance();
            if (character == quote)
            {
                return true;
            }
            if (character == '\\')
            {
                Advance();
            }
        }
        return false;
    }

    [[nodiscard]] bool ReadKey(std::string& key)
    {
        SkipSpaceAndComments();
        if (index >= json.size())
        {
            return false;
        }
        const size_t start = index;
        const bool quoted  = IsQuote(json[index]);
        if (quoted)
        {
            if (! SkipString())
            {
                return false;
            }
        }
        else
        {
            while (index < json.size() && WhitespaceSize(json.substr(index)) == 0)
            {
                const char character = json[index];
                const bool ascii = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9') ||
                                   character == '_' || character == '$';
                if (! ascii && character != '\\' && static_cast<unsigned char>(character) < 0x80)
                {
                    break;
                }
                Advance();
            }
            if (index == start)
            {
                return false;
            }
        }
        const auto token = json.substr(start, index - start);
        if (token.find('\\') == std::string_view::npos)
        {
            key.assign(quoted ? token.substr(1, token.size() - 2) : token);
            return true;
        }
        // An object lets yyjson decode escapes in both quoted and unquoted member names.
        std::string entry{"{"};
        entry.append(token).append(":null}");
        const std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> document{yyjson_read(entry.data(), entry.size(), kSettingsJsonReadFlags),
                                                                               &yyjson_doc_free};
        yyjson_obj_iter members{};
        if (! document || ! yyjson_obj_iter_init(yyjson_doc_get_root(document.get()), &members))
        {
            return false;
        }
        const yyjson_val* name = yyjson_obj_iter_next(&members);
        if (! name)
        {
            return false;
        }
        key.assign(yyjson_get_str(name), yyjson_get_len(name));
        return true;
    }

    [[nodiscard]] bool SkipValue() noexcept
    {
        SkipSpaceAndComments();
        if (index >= json.size())
        {
            return false;
        }
        const char first = json[index];
        if (IsQuote(first))
        {
            return SkipString();
        }
        if (first != '{' && first != '[')
        {
            const size_t begin = index;
            while (index < json.size() && std::string_view(",}]/").find(json[index]) == std::string_view::npos && WhitespaceSize(json.substr(index)) == 0)
            {
                Advance();
            }
            return index > begin;
        }

        size_t depth = 1;
        Advance();
        while (depth != 0)
        {
            SkipSpaceAndComments();
            if (index >= json.size())
            {
                return false;
            }
            const char character = json[index];
            if (IsQuote(character))
            {
                if (! SkipString())
                {
                    return false;
                }
                continue;
            }
            if (character == '{' || character == '[')
            {
                ++depth;
            }
            else if (character == '}' || character == ']')
            {
                --depth;
            }
            Advance();
        }
        return true;
    }
};
