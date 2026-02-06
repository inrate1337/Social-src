#pragma once

#include <Features/Command/Command.hpp>

class StaffCommand : public Command
{
public:
    StaffCommand() : Command("staff") {}
    void execute(const std::vector<std::string>& args) override;
    [[nodiscard]] std::vector<std::string> getAliases() const override;
    [[nodiscard]] std::string getDescription() const override;
    [[nodiscard]] std::string getUsage() const override;
};
