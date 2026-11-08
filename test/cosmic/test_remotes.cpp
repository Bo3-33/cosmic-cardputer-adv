#include "remote_protocol.h"
#include <webos/WebOsKeys.h>
#include <cassert>
#include <cstdio>
int main() {
    using namespace remoteProtocol;
    for (const char *ip : {"192.168.1.10", "10.0.0.8", "172.16.1.100"}) assert(validIp(ip));
    for (const char *ip : {"", "192.168.1", "192.168.1.256", "192.168.1.2:8060", "192.168.01.2", "127.0.0.1", "0.0.0.0", "239.255.255.250", "http://192.168.1.2", "192.168.1.2/path"}) assert(!validIp(ip));
    assert(encodeText("hello world") == "hello%20world");
    assert(encodeText("A&B/#?%+") == "A%26B%2F%23%3F%25%2B");
    using lgremote::RemoteKey;
    const auto *home = lgremote::webos::findKeyMapping(RemoteKey::Home);
    assert(home && std::string(home->button) == "HOME");
    const auto *volume = lgremote::webos::findKeyMapping(RemoteKey::VolumeUp);
    assert(volume && std::string(volume->uri) == "ssap://audio/volumeUp");
    const auto *power = lgremote::webos::findKeyMapping(RemoteKey::PowerOff);
    assert(power && std::string(power->uri) == "ssap://system/turnOff");
    assert(!lgremote::webos::findKeyMapping(RemoteKey::None));
    puts("Remote IP validation, Roku text encoding and LG command mappings passed");
}
