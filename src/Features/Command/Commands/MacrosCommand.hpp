#pragma once

class MacrosCommand : public Command {
public:
    static std::map<int, std::string> mMacros;

    MacrosCommand();
    ~MacrosCommand();

    void execute(const std::vector<std::string>& args) override;
    [[nodiscard]] std::vector<std::string> getAliases() const override;
    [[nodiscard]] std::string getDescription() const override;
    [[nodiscard]] std::string getUsage() const override;

    void onKey(class KeyEvent& event);
};