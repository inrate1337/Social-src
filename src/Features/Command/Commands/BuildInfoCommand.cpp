//
// Created by vastrakai on 7/29/2024.
//

#include "BuildInfoCommand.hpp"

#include <build_info.h>



void BuildInfoCommand::execute(const std::vector<std::string>& args)
{


    // so it doesn't get optimized out
    // auto idHolder = std::make_unique<IdHolder>(DISCORD_USER_ID);
    // idHolder.reset();
#ifdef __DEBUG__
    ChatUtils::displayClientMessage("§6- §eBuild type§7: Debug");
#elif __PRIVATE_BUILD__
    ChatUtils::displayClientMessage("§6- §eBuild type§7: Private");
#else
    ChatUtils::displayClientMessage("§6- §eBuild type§7: Release");
#endif
    ChatUtils::displayClientMessage("§e{}§6 files changed locally compared to the last commit", STRING(SOLSTICE_FILES_CHANGED_COUNT));
}

std::vector<std::string> BuildInfoCommand::getAliases() const
{
    return {"bi"};
}

std::string BuildInfoCommand::getDescription() const
{
    return "Displays information about the current client build";
}

std::string BuildInfoCommand::getUsage() const
{
    return "Usage: .buildinfo";
}
