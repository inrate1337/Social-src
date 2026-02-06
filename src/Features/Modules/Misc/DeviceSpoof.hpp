#pragma once
//
// Created by vastrakai on 7/3/2024.
//


class DeviceSpoof : public ModuleBase<DeviceSpoof>
{
public:
    DeviceSpoof() : ModuleBase("DeviceSpoof", "Spoofs all ur ids", ModuleCategory::Misc, 0, true)
    {
        mNames = {
            {Lowercase, "devicespoof"},
            {LowercaseSpaced, "device spoof"},
            {Normal, "DeviceSpoof"},
            {NormalSpaced, "Device Spoof"}
        };
    }

    static inline unsigned char originalData[7];
    static inline unsigned char patch[] = {0x48, 0xBA, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 };
    static inline void* patchPtr = nullptr;
    static inline std::string DeviceModel;
    static inline bool sUseCustom = false;
    static inline std::string sCustomDeviceModel;
    static inline std::string sCustomDeviceId;
    static inline std::string sCustomSkinId;
    static inline std::string sCustomSelfSignedId;
    static inline int64_t sCustomClientRandomId = 0;

    void inject();
    void eject();
    void spoofMboard();

    void onInit() override;
    void onEnable() override;
    void onDisable() override;
    void onConnectionRequestEvent(class ConnectionRequestEvent& event);
};
