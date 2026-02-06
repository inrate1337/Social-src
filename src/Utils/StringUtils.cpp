#define CRYPTOPP_ENABLE_NAMESPACE_WEAK 1

#include "StringUtils.hpp"

#include <algorithm>
#include <hex.h>
#include <windows.h>
#include <regex>
#include "spdlog/spdlog.h"
#include <cryptopp/files.h>
#include <cryptopp/aes.h>
#include <cryptopp/modes.h>
#include <cryptopp/filters.h>
#include <cryptopp/base64.h>
#include <cryptopp/md5.h>
#include <cryptopp/hex.h>
#include <random>
#include <md5.h>
#include <cryptopp/osrng.h>
#include <sstream>

#include "SysUtils/SHA256.hpp"

std::vector<std::string> motherboardModels = {
        "ASUS ROG Maximus Z790 Extreme",
        "ASUS ROG Maximus Z790 Hero",
        "ASUS ROG Maximus Z790 Apex",
        "ASUS ROG Strix Z790-E Gaming WiFi",
        "ASUS ROG Strix Z790-F Gaming WiFi",
        "ASUS ROG Strix Z790-I Gaming WiFi",
        "ASUS ROG Strix Z790-A Gaming WiFi",
        "ASUS TUF Gaming Z790-Plus WiFi",
        "ASUS Prime Z790-A WiFi",
        "ASUS ProArt Z790-Creator WiFi",
        "ASUS ROG Maximus Z690 Extreme",
        "ASUS ROG Maximus Z690 Formula",
        "ASUS ROG Strix Z690-E Gaming WiFi",
        "ASUS TUF Gaming Z690-Plus WiFi",
        "ASUS ROG Crosshair X670E Extreme",
        "ASUS ROG Crosshair X670E Hero",
        "ASUS ROG Crosshair X670E Gene",
        "ASUS ROG Strix X670E-E Gaming WiFi",
        "ASUS ROG Strix X670E-I Gaming WiFi",
        "ASUS TUF Gaming X670E-Plus WiFi",
        "ASUS Prime X670E-Pro WiFi",
        "ASUS ProArt X670E-Creator WiFi",
        "ASUS ROG Strix B650E-E Gaming WiFi",
        "ASUS ROG Strix B650-A Gaming WiFi",
        "ASUS TUF Gaming B650-Plus WiFi",
        "ASUS ROG Crosshair VIII Extreme",
        "ASUS ROG Crosshair VIII Dark Hero",
        "ASUS ROG Strix X570-E Gaming WiFi II",
        "ASUS TUF Gaming X570-Pro WiFi",
        "MSI MEG Z790 GODLIKE",
        "MSI MEG Z790 ACE",
        "MSI MPG Z790 Carbon WiFi",
        "MSI MPG Z790 Edge WiFi",
        "MSI MAG Z790 Tomahawk WiFi",
        "MSI Pro Z790-A WiFi",
        "MSI MEG Z690 Unify",
        "MSI MPG Z690 Force WiFi",
        "MSI MAG Z690 Torpedo",
        "MSI MEG X670E GODLIKE",
        "MSI MEG X670E ACE",
        "MSI MPG X670E Carbon WiFi",
        "MSI MAG X670E Tomahawk WiFi",
        "MSI PRO X670-P WiFi",
        "MSI MPG B650 Carbon WiFi",
        "MSI MAG B650 Tomahawk WiFi",
        "MSI MAG B650M Mortar WiFi",
        "MSI MEG X570S Unify-X Max",
        "MSI MPG X570S Carbon Max WiFi",
        "MSI MAG X570S Tomahawk Max WiFi",
        "Gigabyte Z790 AORUS XTREME",
        "Gigabyte Z790 AORUS MASTER",
        "Gigabyte Z790 AORUS ELITE AX",
        "Gigabyte Z790 AERO G",
        "Gigabyte Z790 GAMING X AX",
        "Gigabyte Z690 AORUS TACHYON",
        "Gigabyte Z690 VISION D",
        "Gigabyte X670E AORUS XTREME",
        "Gigabyte X670E AORUS MASTER",
        "Gigabyte X670E AORUS ELITE AX",
        "Gigabyte X670 AORUS ELITE AX",
        "Gigabyte B650E AORUS MASTER",
        "Gigabyte B650 AORUS ELITE AX",
        "Gigabyte B650I AORUS ULTRA",
        "Gigabyte X570S AORUS MASTER",
        "Gigabyte X570S AERO G",
        "ASRock Z790 Taichi",
        "ASRock Z790 Steel Legend WiFi",
        "ASRock Z790 Phantom Gaming Riptide",
        "ASRock Z790 Pro RS",
        "ASRock X670E Taichi Carrara",
        "ASRock X670E Taichi",
        "ASRock X670E Steel Legend",
        "ASRock X670E Pro RS",
        "ASRock B650E Taichi",
        "ASRock B650 Steel Legend WiFi",
        "ASRock B650M PG Riptide",
        "ASRock X570 Taichi Razer Edition",
        "ASRock X570S PG Riptide",
        "ASUS ROG Strix Z590-E",
        "MSI MPG Z490 Gaming Edge WiFi",
        "Gigabyte Z590 AORUS Master",
        "ASRock B450M PRO4",
        "ASUS TUF Gaming X570-PLUS",
        "MSI B450 TOMAHAWK MAX",
        "Gigabyte B550 AORUS Elite",
        "ASRock Z490 Phantom Gaming 4",
        "ASUS Prime B560M-A",
        "MSI MAG B550 TOMAHAWK",
        "Gigabyte B450M DS3H",
        "ASRock H570M Pro4",
        "ASUS ROG Crosshair VIII Hero",
        "MSI MPG Z390 Gaming PRO Carbon",
        "Gigabyte Z390 AORUS ULTRA",
        "ASRock B365M PRO4",
        "ASUS Prime Z490-P",
        "MSI MAG Z490 TOMAHAWK",
        "Gigabyte Z490 VISION G",
        "ASRock B460M Steel Legend",
        "ASUS ROG Strix B450-F Gaming",
        "MSI MPG B550 Gaming Plus",
        "Gigabyte Z390 GAMING X",
        "ASRock H410M-HDV",
        "ASUS TUF Z390-PLUS GAMING",
        "MSI B450 GAMING PLUS MAX",
        "Gigabyte B460M DS3H",
        "ASRock X570 Phantom Gaming 4",
        "ASUS Prime B365M-A",
        "MSI Z370 GAMING PLUS",
        "Gigabyte X570 AORUS ELITE",
        "ASRock B550M Steel Legend",
        "ASUS ROG Strix X570-E Gaming",
        "MSI MEG Z490 GODLIKE",
        "Gigabyte A520M DS3H",
        "ASRock Z390 Phantom Gaming 9",
        "ASUS TUF B450-PLUS GAMING",
        "MSI B365M PRO-VDH",
        "Gigabyte Z370 HD3P",
        "ASRock H310M-HDV",
        "ASUS Prime A320M-K",
        "MSI H310M PRO-VDH PLUS",
        "Gigabyte B450 I AORUS PRO WIFI",
        "ASRock B450M Steel Legend",
        "ASUS ROG Maximus XII Hero",
        "MSI MPG Z590 Gaming Carbon WiFi",
        "Gigabyte Z590 AORUS PRO AX",
        "ASRock B365 Phantom Gaming 4",
        "ASUS TUF Gaming B460-PRO",
        "MSI Z490-A PRO",
        "Gigabyte Z390 AORUS PRO WIFI",
        "ASRock H410M-HDV/M.2",
        "ASUS Prime Z390-P",
        "MSI MEG X570 UNIFY",
        "Gigabyte B365M DS3H",
        "ASRock X470 Taichi",
        "ASUS ROG Strix Z490-E Gaming",
        "MSI MAG B460M MORTAR WIFI",
        "Gigabyte Z490M GAMING X",
        "ASRock B460 Steel Legend",
        "ASUS Prime H310M-E R2.0",
        "MSI B450I GAMING PLUS AC",
        "Gigabyte H370 HD3",
        "ASRock B360M Pro4",
        "ASUS TUF Gaming B550M-PLUS",
        "MSI MPG Z490 GAMING EDGE WIFI",
        "Gigabyte B360 AORUS GAMING 3 WIFI",
        "ASRock Z370 Killer SLI",
        "ASUS Prime H310M-D R2.0",
        "MSI Z390-A PRO",
        "Gigabyte B365M D3H",
        "ASRock B365M Phantom Gaming 4",
        "ASUS TUF B360-PRO Gaming",
        "MSI H310M PRO-M2 PLUS",
        "Gigabyte Z370 AORUS ULTRA GAMING WIFI",
        "ASRock H370M-ITX/ac",
        "ASUS Prime B250M-A",
        "MSI B360 GAMING PLUS",
        "Gigabyte H310M S2H",
        "ASRock B250M Pro4",
        "ASUS TUF Z370-PLUS Gaming",
        "MSI B450M GAMING PLUS",
        "Gigabyte B460M GAMING HD",
        "ASRock Z370 Extreme4",
        "ASUS Prime Z270-A",
        "MSI H270 GAMING M3",
        "Gigabyte H270-HD3",
        "ASRock B250M-HDV",
        "ASUS Prime H270-PLUS",
        "MSI B250M Mortar",
        "Gigabyte GA-Z270X-Gaming K5",
        "ASRock H110M-DGS",
        "ASUS Prime A320M-E",
        "MSI H110M PRO-D",
        "Gigabyte GA-H110M-A",
        "ASRock Z170 Extreme6",
        "ASUS Z170-A",
        "MSI Z170A GAMING M5",
        "Gigabyte GA-Z170X-Gaming 7",
        "ASRock H97M Pro4",
        "ASUS H97-PLUS",
        "MSI H97 GAMING 3",
        "Gigabyte GA-H97-D3H",
        "MSI B350 TOMAHAWK",
        "Gigabyte GA-AX370-Gaming K7",
        "ASRock AB350M Pro4",
        "ASUS ROG Crosshair VI Hero",
        "MSI X470 GAMING PRO CARBON",
        "Gigabyte GA-AB350-Gaming 3",
        "ASRock X399 Taichi",
        "ASUS ROG Zenith Extreme",
        "MSI X399 GAMING PRO CARBON AC",
        "Gigabyte X299 AORUS Gaming 7",
        "ASRock X299 Taichi",
        "ASUS Prime X299-DELUXE",
        "MSI X299 SLI PLUS",
        "Gigabyte Z270X-Ultra Gaming",
        "ASRock Z270 Taichi",
        "ASUS ROG Maximus IX Hero",
        "MSI Z270 GAMING PRO CARBON",
        "Gigabyte GA-H270-Gaming 3",
        "ASRock H270M Pro4",
        "ASUS Prime H270M-PLUS",
        "MSI H270 TOMAHAWK",
        "Gigabyte GA-B250M-DS3H",
        "ASUS Prime B250M-PLUS",
        "MSI B250M BAZOOKA",
        "Gigabyte GA-Z170X-Gaming GT",
        "ASRock Z170 Pro4",
        "ASUS Z170-DELUXE",
        "MSI Z170A KRAIT GAMING 3X",
        "Gigabyte GA-Z170X-UD3",
        "ASRock H110M-ITX/ac",
        "ASUS H110M-E/M.2",
        "MSI H110M GAMING",
        "Gigabyte GA-H110M-S2H",
        "ASRock H81M-HDS",
        "ASUS H81M-A",
        "MSI H81M-P33",
        "Gigabyte GA-H81M-DS2V",
        "ASRock Z97 Extreme4",
        "ASUS Z97-PRO",
        "MSI Z97 GAMING 5",
        "Gigabyte GA-Z97X-UD5H",
        "ASRock H97 Anniversary",
        "ASUS H97M-E",
        "MSI H97 PC MATE",
        "Gigabyte GA-H97N-WIFI",
        "ASRock B85M Pro4",
        "ASUS B85M-G",
        "MSI B85-G43 GAMING",
        "Gigabyte GA-B85M-DS3H",
        "ASRock Z87 Extreme4",
        "ASUS Z87-A",
        "MSI Z87-G45 GAMING",
        "Gigabyte GA-Z87X-UD3H",
        "ASRock H87 Pro4",
        "ASUS H87M-E",
        "MSI H87-G43",
        "Gigabyte GA-H87-D3H",
        "ASRock B75 Pro3",
        "ASUS P8B75-M",
        "MSI B75MA-P45",
        "Gigabyte GA-B75M-D3H",
        "ASRock Z77 Extreme4",
        "ASUS P8Z77-V",
        "MSI Z77A-G45",
        "Gigabyte GA-Z77X-UD3H",
        "ASRock H61M-HVS",
        "ASUS P8H61-M LX3",
        "MSI H61M-P31/W8",
        "Gigabyte GA-H61M-DS2",
        "ASRock A320M-HDV",
        "ASUS PRIME A320M-C R2.0",
        "MSI A320M PRO-VD/S",
        "Gigabyte GA-A320M-S2H",
        "ASRock FM2A68M-HD+",
        "ASUS A68HM-K",
        "MSI A68HM GRENADE",
        "Gigabyte GA-F2A68HM-S1",
        "ASRock 970M Pro3",
        "ASUS M5A97 LE R2.0",
        "MSI 970 GAMING",
        "Gigabyte GA-970A-DS3P",
        "ASRock 990FX Extreme6",
        "ASUS SABERTOOTH 990FX R2.0",
        "MSI 990FXA GAMING",
        "Gigabyte GA-990FXA-UD3",
        "ASRock A88M-G/3.1",
        "ASUS A88XM-A",
        "MSI A88XM GAMING",
        "Gigabyte GA-F2A88XM-D3H",
        "ASRock H61M-VG3",
        "ASUS H61M-K",
        "MSI H61M-E33/W8",
        "Gigabyte GA-H61M-USB3V",
        "ASRock Q1900-ITX",
        "ASUS J1800I-C",
        "MSI C847MS-E33",
        "Gigabyte GA-J1800N-D2P"
};

std::string generateMotherboardPC() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> modelDistr(0, motherboardModels.size() - 1);

    int modelIndex = modelDistr(gen);
    std::string mboard = motherboardModels[modelIndex];

    return mboard;
}

std::vector<std::string> motherboardsAndroid = {
        "Samsung Galaxy S24 Ultra",
        "Samsung Galaxy S24+",
        "Samsung Galaxy S24",
        "Samsung Galaxy S23 Ultra",
        "Samsung Galaxy S23+",
        "Samsung Galaxy S23",
        "Samsung Galaxy S23 FE",
        "Samsung Galaxy S22 Ultra",
        "Samsung Galaxy S22+",
        "Samsung Galaxy S22",
        "Samsung Galaxy S21 Ultra",
        "Samsung Galaxy S21+",
        "Samsung Galaxy S21",
        "Samsung Galaxy S21 FE",
        "Samsung Galaxy S20 Ultra",
        "Samsung Galaxy S20+",
        "Samsung Galaxy S20",
        "Samsung Galaxy S20 FE",
        "Samsung Galaxy Note 20 Ultra",
        "Samsung Galaxy Note 20",
        "Samsung Galaxy Note 10+",
        "Samsung Galaxy Note 10",
        "Samsung Galaxy Z Fold 6",
        "Samsung Galaxy Z Fold 5",
        "Samsung Galaxy Z Fold 4",
        "Samsung Galaxy Z Fold 3",
        "Samsung Galaxy Z Fold 2",
        "Samsung Galaxy Z Flip 6",
        "Samsung Galaxy Z Flip 5",
        "Samsung Galaxy Z Flip 4",
        "Samsung Galaxy Z Flip 3",
        "Samsung Galaxy Z Flip",
        "Samsung Galaxy A73 5G",
        "Samsung Galaxy A72",
        "Samsung Galaxy A55",
        "Samsung Galaxy A54",
        "Samsung Galaxy A53",
        "Samsung Galaxy A52s 5G",
        "Samsung Galaxy A52",
        "Samsung Galaxy A35",
        "Samsung Galaxy A34",
        "Samsung Galaxy A33",
        "Samsung Galaxy A32",
        "Samsung Galaxy A25",
        "Samsung Galaxy A24",
        "Samsung Galaxy A23",
        "Samsung Galaxy A15",
        "Samsung Galaxy A14",
        "Samsung Galaxy A13",
        "Samsung Galaxy A12",
        "Samsung Galaxy A05s",
        "Samsung Galaxy A04s",
        "Samsung Galaxy M55",
        "Samsung Galaxy M54",
        "Samsung Galaxy M34",
        "Samsung Galaxy M32",
        "Samsung Galaxy M14",
        "Samsung Galaxy F62",
        "Samsung Galaxy F54",
        "Samsung Galaxy F23",
        "Google Pixel 9 Pro XL",
        "Google Pixel 9 Pro",
        "Google Pixel 9",
        "Google Pixel 9 Pro Fold",
        "Google Pixel 8 Pro",
        "Google Pixel 8",
        "Google Pixel 8a",
        "Google Pixel 7 Pro",
        "Google Pixel 7",
        "Google Pixel 7a",
        "Google Pixel 6 Pro",
        "Google Pixel 6",
        "Google Pixel 6a",
        "Google Pixel 5",
        "Google Pixel 5a",
        "Google Pixel 4 XL",
        "Google Pixel 4",
        "Google Pixel 4a 5G",
        "Google Pixel 4a",
        "Google Pixel 3 XL",
        "Google Pixel 3",
        "Google Pixel 3a XL",
        "Google Pixel 3a",
        "OnePlus 12",
        "OnePlus 12R",
        "OnePlus 11",
        "OnePlus 10 Pro",
        "OnePlus 10T",
        "OnePlus 9 Pro",
        "OnePlus 9",
        "OnePlus 9R",
        "OnePlus 9RT",
        "OnePlus 8 Pro",
        "OnePlus 8T",
        "OnePlus 8",
        "OnePlus 7T Pro",
        "OnePlus 7T",
        "OnePlus 7 Pro",
        "OnePlus Open",
        "OnePlus Nord 4",
        "OnePlus Nord 3",
        "OnePlus Nord 2T",
        "OnePlus Nord 2",
        "OnePlus Nord CE 4",
        "OnePlus Nord CE 3",
        "OnePlus Nord N30",
        "OnePlus Nord N20",
        "OnePlus Nord N10",
        "Xiaomi 14 Ultra",
        "Xiaomi 14 Pro",
        "Xiaomi 14",
        "Xiaomi 13 Ultra",
        "Xiaomi 13 Pro",
        "Xiaomi 13",
        "Xiaomi 13T Pro",
        "Xiaomi 13T",
        "Xiaomi 12S Ultra",
        "Xiaomi 12 Pro",
        "Xiaomi 12",
        "Xiaomi 12T Pro",
        "Xiaomi 12T",
        "Xiaomi Mi 11 Ultra",
        "Xiaomi Mi 11",
        "Xiaomi Mi 11i",
        "Xiaomi Mi 11 Lite 5G",
        "Xiaomi Mi 10 Pro",
        "Xiaomi Mi 10T Pro",
        "Xiaomi Mi 10",
        "Xiaomi Redmi Note 13 Pro+",
        "Xiaomi Redmi Note 13 Pro",
        "Xiaomi Redmi Note 13",
        "Xiaomi Redmi Note 12 Pro+",
        "Xiaomi Redmi Note 12 Pro",
        "Xiaomi Redmi Note 12",
        "Xiaomi Redmi Note 11 Pro+",
        "Xiaomi Redmi Note 11 Pro",
        "Xiaomi Redmi Note 11",
        "Xiaomi Redmi Note 11S",
        "Xiaomi Redmi Note 10 Pro",
        "Xiaomi Redmi Note 10",
        "Xiaomi Redmi Note 9 Pro",
        "Xiaomi Redmi Note 8 Pro",
        "Xiaomi Redmi Note 8",
        "Xiaomi Redmi 13C",
        "Xiaomi Redmi 12",
        "Xiaomi Redmi 10",
        "Xiaomi Redmi A3",
        "Xiaomi Redmi K70 Pro",
        "Xiaomi Redmi K60 Ultra",
        "Xiaomi Redmi K50 Gaming",
        "Xiaomi Mix Fold 4",
        "Xiaomi Mix Fold 3",
        "Poco F6 Pro",
        "Poco F6",
        "Poco F5 Pro",
        "Poco F5",
        "Poco F4 GT",
        "Poco F4",
        "Poco F3",
        "Poco X6 Pro",
        "Poco X6",
        "Poco X5 Pro",
        "Poco X4 Pro 5G",
        "Poco X3 Pro",
        "Poco X3 NFC",
        "Poco M6 Pro",
        "Poco M5",
        "Nothing Phone (2)",
        "Nothing Phone (1)",
        "Nothing Phone (2a)",
        "Sony Xperia 1 VI",
        "Sony Xperia 1 V",
        "Sony Xperia 1 IV",
        "Sony Xperia 1 III",
        "Sony Xperia 1 II",
        "Sony Xperia 5 V",
        "Sony Xperia 5 IV",
        "Sony Xperia 5 III",
        "Sony Xperia 10 VI",
        "Sony Xperia 10 V",
        "Sony Xperia PRO-I",
        "Motorola Edge 50 Ultra",
        "Motorola Edge 50 Pro",
        "Motorola Edge 50 Fusion",
        "Motorola Edge 40 Pro",
        "Motorola Edge 40",
        "Motorola Edge 30 Ultra",
        "Motorola Edge 30 Pro",
        "Motorola Edge 20 Pro",
        "Motorola Razr 50 Ultra",
        "Motorola Razr 50",
        "Motorola Razr 40 Ultra",
        "Motorola Razr 40",
        "Motorola Razr 2022",
        "Motorola Moto G85",
        "Motorola Moto G84",
        "Motorola Moto G73",
        "Motorola Moto G54",
        "Motorola Moto G power 5G",
        "Motorola Moto G Stylus 5G",
        "Motorola Moto G100",
        "Oppo Find X7 Ultra",
        "Oppo Find X6 Pro",
        "Oppo Find X5 Pro",
        "Oppo Find X3 Pro",
        "Oppo Find N3",
        "Oppo Find N3 Flip",
        "Oppo Find N2 Flip",
        "Oppo Reno 12 Pro",
        "Oppo Reno 11 Pro",
        "Oppo Reno 10 Pro+",
        "Oppo Reno 8 Pro",
        "Oppo Reno 7",
        "Oppo A98",
        "Oppo A78",
        "Oppo A58",
        "Vivo X100 Pro",
        "Vivo X100 Ultra",
        "Vivo X90 Pro+",
        "Vivo X80 Pro",
        "Vivo V30 Pro",
        "Vivo V29 Pro",
        "Vivo V27 Pro",
        "Vivo T2 Pro",
        "Vivo Y200",
        "Vivo iQOO 12 Pro",
        "Vivo iQOO 11",
        "Vivo iQOO Neo 9",
        "Realme GT 6",
        "Realme GT 5 Pro",
        "Realme GT 2 Pro",
        "Realme GT Neo 6",
        "Realme GT Neo 5",
        "Realme 12 Pro+",
        "Realme 11 Pro+",
        "Realme 10 Pro+",
        "Realme 9 Pro+",
        "Realme 8 Pro",
        "Realme C67",
        "Realme C55",
        "Asus ROG Phone 8 Pro",
        "Asus ROG Phone 7 Ultimate",
        "Asus ROG Phone 6 Pro",
        "Asus ROG Phone 5",
        "Asus Zenfone 10",
        "Asus Zenfone 9",
        "Asus Zenfone 8",
        "Huawei Pura 70 Ultra",
        "Huawei Pura 70 Pro",
        "Huawei P60 Pro",
        "Huawei P50 Pro",
        "Huawei P40 Pro",
        "Huawei Mate 60 Pro",
        "Huawei Mate 50 Pro",
        "Huawei Mate 40 Pro",
        "Huawei Mate X5",
        "Huawei Mate X3",
        "Huawei Nova 12 Pro",
        "Huawei Nova 11",
        "Honor Magic 6 Pro",
        "Honor Magic 5 Pro",
        "Honor Magic 4 Pro",
        "Honor Magic V3",
        "Honor Magic V2",
        "Honor 200 Pro",
        "Honor 100",
        "Honor 90",
        "Honor 70",
        "Honor X9b",
        "ZTE Nubia Z60 Ultra",
        "ZTE Nubia Z50 Ultra",
        "ZTE Red Magic 9 Pro",
        "ZTE Red Magic 8S Pro",
        "ZTE Red Magic 7",
        "ZTE Axon 60 Ultra",
        "ZTE Axon 50 Ultra"
};

std::string generateDeviceAndroid() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> modelDistr(0, motherboardsAndroid.size() - 1);

    int modelIndex = modelDistr(gen);
    std::string mboard = motherboardsAndroid[modelIndex];

    std::transform(mboard.begin(), mboard.end(), mboard.begin(), ::toupper);

    return mboard;
}

std::vector<std::string> modelsiOS = {
        "iPhone5,3", "iPhone5,4", "iPhone6,1", "iPhone6,2", "iPhone7,2", "iPhone7,1",
        "iPhone8,1", "iPhone8,2", "iPhone8,4", "iPhone9,1", "iPhone9,3", "iPhone9,2",
        "iPhone9,4", "iPhone10,1", "iPhone10,4", "iPhone10,2", "iPhone10,5", "iPhone10,3",
        "iPhone10,6", "iPhone11,8", "iPhone11,2", "iPhone11,6", "iPhone12,1", "iPhone12,3",
        "iPhone12,5", "iPhone12,8", "iPhone13,1", "iPhone13,2", "iPhone13,3", "iPhone13,4",
        "iPhone14,4", "iPhone14,5", "iPhone14,2", "iPhone14,3", "iPhone14,6", "iPhone14,7",
        "iPhone14,8", "iPhone15,2", "iPhone15,3", "iPhone15,4", "iPhone15,5", "iPhone16,1",
        "iPhone16,2", "iPad6,11", "iPad6,12", "iPad7,5", "iPad7,6", "iPad7,11", "iPad7,12",
        "iPad11,6", "iPad11,7", "iPad12,1", "iPad12,2", "iPad13,18", "iPad13,19", "iPad4,4",
        "iPad4,5", "iPad4,6", "iPad4,7", "iPad4,8", "iPad4,9", "iPad5,1", "iPad5,2", "iPad11,1",
        "iPad11,2", "iPad14,1", "iPad14,2", "iPad4,1", "iPad4,2", "iPad4,3", "iPad5,3", "iPad5,4",
        "iPad11,3", "iPad11,4", "iPad13,1", "iPad13,2", "iPad13,16", "iPad13,17", "iPad6,3", "iPad6,4",
        "iPad7,3", "iPad7,4", "iPad8,1", "iPad8,2", "iPad8,3", "iPad8,4", "iPad8,9", "iPad8,10", "iPad13,4",
        "iPad13,5", "iPad13,6", "iPad13,7", "iPad14,3", "iPad14,4", "iPad6,7", "iPad6,8", "iPad7,1", "iPad7,2",
        "iPad8,5", "iPad8,6", "iPad8,7", "iPad8,8", "iPad8,11", "iPad8,12", "iPad13,8", "iPad13,9", "iPad13,10",
        "iPad13,11", "iPad14,5", "iPad14,6"
};

std::string generateDeviceiOS() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> modelDistr(0, modelsiOS.size() - 1);

    int modelIndex = modelDistr(gen);

    return modelsiOS[modelIndex];
}

std::string StringUtils::generateMboard(int index) {
    switch (index)
    {
        case 1:
            return generateDeviceAndroid();
        case 2:
            return generateDeviceiOS();
        case 7:
            return generateMotherboardPC();
        default:
            return generateMotherboardPC();
    }
}

static std::string namespaceUUID = "1234567890abcdef1234567890abcdef";

std::string generateRandomName(size_t length) {
    static const char alphabet[] =
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static std::mt19937 rng(std::random_device{}());
    static std::uniform_int_distribution<size_t> dist(0, sizeof(alphabet) - 2);

    std::string name;
    name.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        name += alphabet[dist(rng)];
    }

    return name;
}

std::string generateUUID3() {
    std::array<uint8_t, 16> namespaceBytes;
    for (size_t i = 0; i < 16; ++i) {
        namespaceBytes[i] = static_cast<uint8_t>(std::stoi(namespaceUUID.substr(i * 2, 2), nullptr, 16));
    }

    std::string combined(namespaceBytes.begin(), namespaceBytes.end());
    std::string name = generateRandomName(32);
    combined += name;

    CryptoPP::Weak::MD5 hash;
    std::array<uint8_t, CryptoPP::Weak::MD5::DIGESTSIZE> digest;
    hash.CalculateDigest(digest.data(), reinterpret_cast<const uint8_t*>(combined.data()), combined.size());

    digest[6] = (digest[6] & 0x0F) | 0x30;
    digest[8] = (digest[8] & 0x3F) | 0x80;

    std::ostringstream uuid;
    uuid << std::hex << std::setfill('0');
    for (size_t i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            uuid << '-';
        }
        uuid << std::setw(2) << static_cast<int>(digest[i]);
    }

    return uuid.str();
}

std::string generateUUID4() {
    CryptoPP::AutoSeededRandomPool prng;

    unsigned char randomBytes[16];
    prng.GenerateBlock(randomBytes, sizeof(randomBytes));

    randomBytes[6] = (randomBytes[6] & 0x0F) | 0x40;
    randomBytes[8] = (randomBytes[8] & 0x3F) | 0x80;

    std::stringstream uuidStream;
    uuidStream << std::hex;

    for (int i = 0; i < 16; ++i) {
        uuidStream << (int)(randomBytes[i] >> 4);
        uuidStream << (int)(randomBytes[i] & 0x0F);
    }

    return uuidStream.str();
}

std::string StringUtils::generateUUID(int index) {
    std::string uuid;

    switch (index)
    {
        case 1: // android
            uuid = generateUUID4();
            return uuid;
            break;
        case 2: // ios
            uuid = toUpper(generateUUID4());
            return uuid;
            break;
        case 7: // windows
            uuid = generateUUID3();
            return uuid;
            break;
        default:
            std::string name = generateRandomName(32);
            uuid = generateUUID3();
            return uuid;
            break;
    }
}

int64_t StringUtils::generateCID() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis(1000000000000000000ULL, 9999999999999999999ULL);
    static std::uniform_int_distribution<int> sign_dis(0, 1);

    uint64_t id = dis(gen);
    if (sign_dis(gen) == 1) {
        return static_cast<int64_t>(-id);
    } else {
        return static_cast<int64_t>(id);
    }
}

bool StringUtils::startsWith(std::string_view str, std::string_view prefix)
{
    return str.substr(0, prefix.size()) == prefix;
}

bool StringUtils::endsWith(std::string_view str, std::string_view suffix)
{
    return str.substr(str.size() - suffix.size(), suffix.size()) == suffix;
}

std::string StringUtils::RemoveColorCodes(std::string str) {
    return std::regex_replace(std::regex_replace(str, std::regex("§."), ""), std::regex("&."), "");
}

std::string_view StringUtils::trim(std::string_view str)
{
    size_t start = 0;
    size_t end = str.size();

    while (start < end && std::isspace(str[start])) {
        start++;
    }

    while (end > start && std::isspace(str[end - 1])) {
        end--;
    }

    return str.substr(start, end - start);
}

std::vector<std::string> StringUtils::split(std::string_view str, char delimiter)
{
    std::vector<std::string> result;
    size_t start = 0;
    size_t end = 0;

    while ((end = str.find(delimiter, start)) != std::string::npos)
    {
        result.emplace_back(str.substr(start, end - start));
        start = end + 1;
    }

    result.emplace_back(str.substr(start));

    return result;
}

// toLower and toUpper
std::string StringUtils::toLower(std::string str)
{
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) { return std::tolower(c); });
    return str;
}

std::string StringUtils::toUpper(std::string str)
{
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) { return std::toupper(c); });
    return str;
}

bool StringUtils::equalsIgnoreCase(const std::string& str1, const std::string& str2)
{
    return toLower(str1) == toLower(str2);
}

bool StringUtils::containsIgnoreCase(const std::string& str, const std::string& subStr)
{
    return toLower(str).find(toLower(subStr)) != std::string::npos;
}

bool StringUtils::containsAnyIgnoreCase(const std::string& str, const std::vector<std::string>& strVector)
{
    if (std::ranges::any_of(strVector, [&str](const std::string& subStr) { return containsIgnoreCase(str, subStr); }))
    {
        return true;
    }

    return false;
}

std::string StringUtils::getClipboardText()
{
    // Try opening the clipboard
    if (! OpenClipboard(nullptr))
    {
        spdlog::error("Failed to open clipboard: {}", GetLastError());
        return "";
    }

    // Get handle of clipboard object for ANSI text
    HANDLE hData = GetClipboardData(CF_TEXT);
    if (hData == nullptr)
    {
        spdlog::error("Failed to get clipboard text: {}", GetLastError());
        return "";
    }

    // Lock the handle to get the actual text pointer
    char * pszText = static_cast<char*>( GlobalLock(hData) );
    if (pszText == nullptr)
    {
        spdlog::error("Failed to get clipboard text: {}", GetLastError());
        return "";
    }

    // Save text in a string class instance
    std::string text( pszText );

    // Release the lock
    GlobalUnlock( hData );

    // Release the clipboard
    CloseClipboard();

    return text;
}

std::string StringUtils::join(const std::vector<std::string>& strings, const std::string& delimiter)
{
    std::string result;
    for (size_t i = 0; i < strings.size(); i++)
    {
        result += strings[i];
        if (i != strings.size() - 1)
        {
            result += delimiter;
        }
    }

    return result;
}

std::string StringUtils::replaceAll(std::string& string, const std::string& from, const std::string& to)
{
    size_t start_pos = 0;
    while ((start_pos = string.find(from, start_pos)) != std::string::npos)
    {
        string.replace(start_pos, from.length(), to);
        start_pos += to.length();
    }

    return string;
}


std::string StringUtils::sha256(const std::string& str)
{
    return ::SHA256::hash(str);
}

std::string StringUtils::fromBase64(const std::string& str)
{
    return Base64::decode(str);
}

std::string StringUtils::toBase64(const std::string& str)
{
    return Base64::encode(str);
}

std::string StringUtils::getRelativeTime(std::chrono::system_clock::time_point time)
{
    auto now = std::chrono::system_clock::now();

    auto diff = now - time;

    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(diff);
    if (seconds.count() < 60) return std::to_string(seconds.count()) + " seconds ago";

    auto minutes = std::chrono::duration_cast<std::chrono::minutes>(diff);
    if (minutes.count() < 60) return std::to_string(minutes.count()) + " minutes ago";

    auto hours = std::chrono::duration_cast<std::chrono::hours>(diff);
    if (hours.count() < 24) return std::to_string(hours.count()) + " hours ago";

    auto days = std::chrono::duration_cast<std::chrono::days>(diff);
    if (days.count() < 30) return std::to_string(days.count()) + " days ago";

    auto months = std::chrono::duration_cast<std::chrono::months>(diff);
    if (months.count() < 12) return std::to_string(months.count()) + " months ago";

    auto years = std::chrono::duration_cast<std::chrono::years>(diff);
    return std::to_string(years.count()) + " years ago";
}

/*
public class StringCipher
{
    public static string Encode(string input)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(input);

        for (int i = 0; i < bytes.Length - 1; i += 2) (bytes[i], bytes[i + 1]) = (bytes[i + 1], bytes[i]);
        for (int i = 0; i < bytes.Length; i++) bytes[i] = (byte)~bytes[i];

        return Convert.ToBase64String(bytes);
    }

    public static string Decode(string input)
    {
        byte[] bytes = Convert.FromBase64String(input);

        for (int i = 0; i < bytes.Length; i++) bytes[i] = (byte)~bytes[i];
        for (int i = 0; i < bytes.Length - 1; i += 2) (bytes[i], bytes[i + 1]) = (bytes[i + 1], bytes[i]);

        return Encoding.UTF8.GetString(bytes);
    }
}
*/




std::string StringUtils::decode(const std::string& str)
{
    std::vector<uint8_t> bytes = Base64::decodeBytes(str);

    for (auto& byte : bytes) {
        byte = ~byte;
    }

    for (size_t i = 0; i < bytes.size() - 1; i += 2) {
        std::swap(bytes[i], bytes[i + 1]);
    }

    return std::string(bytes.begin(), bytes.end());
}

std::string StringUtils::encode(const std::string& input)
{
    std::vector<uint8_t> bytes(input.begin(), input.end());

    for (size_t i = 0; i < bytes.size() - 1; i += 2) {
        std::swap(bytes[i], bytes[i + 1]);
    }

    for (auto& byte : bytes) {
        byte = ~byte;
    }

    return Base64::encodeBytes(bytes);
}
/*
public static string Encrypt(string plaintext, string key)
    {
        // Truncate key to 16 bytes (128-bit)
        byte[] truncatedKey = new byte[16];
        Array.Copy(Encoding.UTF8.GetBytes(key), truncatedKey, Math.Min(16, key.Length));

        // simple encryption
        byte[] plaintextBytes = Encoding.UTF8.GetBytes(plaintext);
        byte[] ciphertextBytes = new byte[plaintextBytes.Length];
        for (int i = 0; i < plaintextBytes.Length; i++)
        {
            ciphertextBytes[i] = (byte)(plaintextBytes[i] ^ truncatedKey[i % 16]);
        }

        return Convert.ToBase64String(ciphertextBytes);
    }

    public static string Decrypt(string ciphertext, string key)
    {
        // Truncate key to 16 bytes (128-bit)
        byte[] truncatedKey = new byte[16];
        Array.Copy(Encoding.UTF8.GetBytes(key), truncatedKey, Math.Min(16, key.Length));

        // simple decryption
        byte[] ciphertextBytes = Convert.FromBase64String(ciphertext);
        byte[] plaintextBytes = new byte[ciphertextBytes.Length];
        for (int i = 0; i < ciphertextBytes.Length; i++)
        {
            plaintextBytes[i] = (byte)(ciphertextBytes[i] ^ truncatedKey[i % 16]);
        }

        return Encoding.UTF8.GetString(plaintextBytes);
    }*/

using namespace CryptoPP;
class EncUtils
{
public:
    static std::string Encrypt(const std::string& plaintext, const std::string& key) {
        // Truncate key to 16 bytes
        std::string truncatedKey = key.substr(0, 16);
        // add 2 to every byte in the key
        for (auto& byte : truncatedKey) {
            byte += 2;
        }

        // Simple encryption
        std::vector<unsigned char> plaintextBytes(plaintext.begin(), plaintext.end());
        std::vector<unsigned char> ciphertextBytes(plaintextBytes.size());

        for (size_t i = 0; i < plaintextBytes.size(); i++) {
            ciphertextBytes[i] = plaintextBytes[i] ^ truncatedKey[i % 16];
        }

        return Base64::encode(std::string(ciphertextBytes.begin(), ciphertextBytes.end()));
    }

    static std::string Decrypt(const std::string& ciphertext, const std::string& key) {
        // Truncate key to 16 bytes
        std::string truncatedKey = key.substr(0, 16);
        // add 2 to every byte in the key
        for (auto& byte : truncatedKey) {
            byte += 2;
        }

        // Simple decryption
        std::vector<unsigned char> ciphertextBytes = Base64::decodeBytes(ciphertext);
        std::vector<unsigned char> plaintextBytes(ciphertextBytes.size());

        for (size_t i = 0; i < ciphertextBytes.size(); i++) {
            plaintextBytes[i] = ciphertextBytes[i] ^ truncatedKey[i % 16];
        }

        return std::string(plaintextBytes.begin(), plaintextBytes.end());
    }
};

std::string StringUtils::encrypt(const std::string& input, const std::string& key) {
    return EncUtils::Encrypt(input, key);
}

std::string StringUtils::decrypt(const std::string& input, const std::string& key) {
    return EncUtils::Decrypt(input, key);
}

std::string StringUtils::toHex(const std::vector<uint8_t>& data)
{
    return ""; // not implemented
}

bool StringUtils::contains(const std::string& str, const std::string& subStr)
{
    return str.find(subStr) != std::string::npos;
}

std::string StringUtils::replace(const std::string& str, const std::string& from, const std::string& to)
{
    // replace only the first occurrence
    std::string result = str;
    size_t start_pos = result.find(from);
    if (start_pos != std::string::npos)
    {
        result.replace(start_pos, from.length(), to);
    }
    return result;
}

std::string StringUtils::randomString(int length)
{
    std::string str("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");

    std::random_device rd;
    std::mt19937 generator(rd());

    std::ranges::shuffle(str, generator);

    return str.substr(0, length);};
