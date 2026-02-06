//
// Created by vastrakai on 6/30/2024.
//

#include "Setting.hpp"
#include <Utils/StringUtils.hpp>
#include <algorithm>
#include <cctype>

bool Setting::parse(const std::string& value)
{
    if (mType == SettingType::Bool)
    {
        if (value == "true" || value == "1")
        {
            static_cast<BoolSetting*>(this)->mValue = true;
            return true;
        }
        if (value == "false" || value == "0")
        {
            static_cast<BoolSetting*>(this)->mValue = false;
            return true;
        }
        return false;
    }
    if (mType == SettingType::Number)
    {
        try
        {
            reinterpret_cast<NumberSetting*>(this)->mValue = std::stof(value);
            return true;
        }
        catch (const std::exception&)
        {
            return false;
        }
    }
    if (mType == SettingType::Enum)
    {
        auto* enumSetting = this->asEnumSettingBase();
        if (!enumSetting) return false;
        int index = 0;
        std::string filteredValue = StringUtils::toLower(value);
        std::erase_if(filteredValue, [](unsigned char c) { return !std::isalnum(c); });
        for (auto enumValue : enumSetting->getValues())
        {
            enumValue = StringUtils::toLower(enumValue);
            if (enumValue == filteredValue)
            {
                enumSetting->setIndex(index);
                return true;
            }

            index++;
        }
        return false;
    }
    if (mType == SettingType::Color)
    {
        try
        {
            unsigned long val = std::stoul(value, nullptr, 16);
            reinterpret_cast<ColorSetting*>(this)->setFromHex(val);
            return true;
        }
        catch (const std::exception&)
        {
            return false;
        }
    }
    return false;
}
