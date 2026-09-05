#pragma once
#include "../../Common/PlugInterfaces/Factory.h"

namespace AVControl
{
inline constexpr char PluginId[] = "builtin.av-control";
inline constexpr char TypeId[] = "av-control";
inline constexpr char DefaultsJson[] = R"json({"profiles":[]})json";
inline constexpr char SchemaJson[] = R"json({"type":"object","additionalProperties":false,"properties":{"profiles":{"type":"array","maxItems":4,"items":{"type":"object","additionalProperties":false,"required":["id","name","outputId","microphoneId","cameraId","audioRoles","restoreLevels","outputLevel","microphoneLevel"],"properties":{"id":{"type":"string","minLength":1,"maxLength":32,"pattern":"^[A-Za-z0-9_-]+$"},"name":{"type":"string","minLength":1,"maxLength":48},"outputId":{"type":"string","minLength":1,"maxLength":1024},"microphoneId":{"type":"string","minLength":1,"maxLength":1024},"cameraId":{"type":"string","minLength":1,"maxLength":1024},"audioRoles":{"type":"string","enum":["all","communications"]},"restoreLevels":{"type":"boolean"},"outputLevel":{"type":"integer","minimum":0,"maximum":100},"microphoneLevel":{"type":"integer","minimum":0,"maximum":100}}}}}})json";
inline constexpr RedXePluginSettingsContract SettingsContract{
    sizeof(RedXePluginSettingsContract), SchemaJson, sizeof(SchemaJson) - 1, DefaultsJson, sizeof(DefaultsJson) - 1};
}
