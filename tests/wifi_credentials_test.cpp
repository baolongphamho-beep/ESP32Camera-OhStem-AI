#include <cassert>
#include <cstdio>
#include <string>
#include "wifi_credentials.h"

int main() {
    WifiCredentials cfg;
    assert(!prepareWifiCredentials("Lab", "", cfg) && cfg.open);
    assert(cfg.ssid[3] == 0 && cfg.password[0] == 0);
    assert(!prepareWifiCredentials("Lab", "password", cfg) && !cfg.open);

    const std::string ssid(32, 'S');
    const std::string passphrase(63, 'p');
    assert(!prepareWifiCredentials(ssid, passphrase, cfg));
    assert(std::string(cfg.ssid.begin(), cfg.ssid.end()) == ssid);
    assert(std::string(cfg.password.begin(), cfg.password.begin() + 63) == passphrase);
    assert(cfg.password[63] == 0);

    const std::string hexKey = std::string(32, 'a') + std::string(32, 'F');
    assert(!prepareWifiCredentials(ssid, hexKey, cfg));
    assert(std::string(cfg.password.begin(), cfg.password.end()) == hexKey);
    const WifiCredentials saved = cfg;
    for (const auto& password : {std::string("short"), std::string(64, 'z'), std::string(65, 'a')}) {
        assert(prepareWifiCredentials(ssid, password, cfg));
        assert(cfg.password == saved.password && cfg.ssid == saved.ssid);
    }
    assert(prepareWifiCredentials(std::string(33, 'S'), "password", cfg));
    assert(prepareWifiCredentials(std::string("a\0b", 3), "password", cfg));
    assert(prepareWifiCredentials("Lab", std::string("password\0", 9), cfg));

    // Byte counts, not character counts: UTF-8 may use multiple bytes.
    std::string utf8;
    for (unsigned i = 0; i < 16; ++i) utf8 += "\xc3\xa9";
    assert(!prepareWifiCredentials(utf8, "password", cfg));
    assert(prepareWifiCredentials(utf8 + "\xc3\xa9", "password", cfg));
    // Replacing a long key must clear its tail; empty SSID supports disabling.
    assert(!prepareWifiCredentials("", "", cfg) && cfg.open);
    for (auto b : cfg.ssid) assert(b == 0);
    for (auto b : cfg.password) assert(b == 0);
    std::puts("wifi_credentials_test: PASS");
}
