#pragma once

#include <NewLight.hpp>
#include <Features/FeatureManager.hpp>
#include "spdlog/spdlog.h"

class GotoCommand : public Command {
public:
    GotoCommand() : Command("goto") {}
    void execute(const std::vector<std::string>& args) override;
    [[nodiscard]] std::vector<std::string> getAliases() const override;
    [[nodiscard]] std::string getDescription() const override;
    [[nodiscard]] std::string getUsage() const override;
};