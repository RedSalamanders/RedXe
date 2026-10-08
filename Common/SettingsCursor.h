#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

// Strict reader for the compact factory configuration envelope `{"plugin":{},"instance":<effective-settings>}` the
// host hands a bundled widget DLL (Plugins_API.md). It reads JSON whitespace and punctuation, strings without escapes
// or control characters, `true` and `false`, and unsigned 32-bit integers without a sign, fraction, exponent, or
// leading zero. Anything else fails the read, so a plugin never accepts a value its published schema does not allow.
//
// Shared by the bundled widget DLLs that parse their own settings (Studio Clock, Desk Clock, Matrix Rain, 5H4D3R5).
// Plain value type over borrowed text: no allocation, no Win32.
class RedXeSettingsCursor final
{
  public:
    constexpr explicit RedXeSettingsCursor(std::string_view text) noexcept : _text(text) {}

    constexpr void SkipWhitespace() noexcept
    {
        while (_offset < _text.size())
        {
            const char value = _text[_offset];
            if (value != ' ' && value != '\t' && value != '\r' && value != '\n')
            {
                break;
            }
            ++_offset;
        }
    }

    [[nodiscard]] constexpr bool Consume(char expected) noexcept
    {
        SkipWhitespace();
        if (_offset >= _text.size() || _text[_offset] != expected)
        {
            return false;
        }
        ++_offset;
        return true;
    }

    [[nodiscard]] constexpr bool ReadString(std::string_view& value) noexcept
    {
        SkipWhitespace();
        if (_offset >= _text.size() || _text[_offset] != '"')
        {
            return false;
        }
        const size_t start = ++_offset;
        while (_offset < _text.size() && _text[_offset] != '"')
        {
            const unsigned char character = static_cast<unsigned char>(_text[_offset]);
            if (character < 0x20U || character == '\\')
            {
                return false;
            }
            ++_offset;
        }
        if (_offset >= _text.size())
        {
            return false;
        }
        value = _text.substr(start, _offset - start);
        ++_offset;
        return true;
    }

    [[nodiscard]] constexpr bool ReadBoolean(bool& value) noexcept
    {
        SkipWhitespace();
        constexpr std::string_view trueText = "true";
        constexpr std::string_view falseText = "false";
        if (_text.substr(_offset, trueText.size()) == trueText)
        {
            _offset += trueText.size();
            value = true;
            return true;
        }
        if (_text.substr(_offset, falseText.size()) == falseText)
        {
            _offset += falseText.size();
            value = false;
            return true;
        }
        return false;
    }

    [[nodiscard]] constexpr bool ReadUnsigned(uint32_t& value) noexcept
    {
        SkipWhitespace();
        if (_offset >= _text.size() || _text[_offset] < '0' || _text[_offset] > '9')
        {
            return false;
        }
        const bool leadingZero = _text[_offset] == '0';
        uint64_t parsed = 0;
        size_t digits = 0;
        while (_offset < _text.size() && _text[_offset] >= '0' && _text[_offset] <= '9')
        {
            parsed = parsed * 10U + static_cast<uint64_t>(_text[_offset] - '0');
            if (parsed > std::numeric_limits<uint32_t>::max())
            {
                return false;
            }
            ++_offset;
            ++digits;
        }
        if (leadingZero && digits != 1)
        {
            return false;
        }
        value = static_cast<uint32_t>(parsed);
        return true;
    }

    [[nodiscard]] constexpr bool AtEnd() noexcept
    {
        SkipWhitespace();
        return _offset == _text.size();
    }

  private:
    std::string_view _text;
    size_t _offset = 0;
};

[[nodiscard]] constexpr int RedXeHexDigitValue(char value) noexcept
{
    if (value >= '0' && value <= '9')
    {
        return value - '0';
    }
    if (value >= 'A' && value <= 'F')
    {
        return value - 'A' + 10;
    }
    if (value >= 'a' && value <= 'f')
    {
        return value - 'a' + 10;
    }
    return -1;
}

// An exact `#RRGGBB` string with case-insensitive hexadecimal digits, as 0xRRGGBB.
[[nodiscard]] constexpr bool RedXeParseHexColor(std::string_view text, uint32_t& color) noexcept
{
    if (text.size() != 7 || text[0] != '#')
    {
        return false;
    }
    uint32_t parsed = 0;
    for (size_t index = 1; index < text.size(); ++index)
    {
        const int digit = RedXeHexDigitValue(text[index]);
        if (digit < 0)
        {
            return false;
        }
        parsed = (parsed << 4U) | static_cast<uint32_t>(digit);
    }
    color = parsed;
    return true;
}
