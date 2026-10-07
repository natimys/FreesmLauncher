// SPDX-License-Identifier: GPL-3.0-only
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

namespace {
std::string value(const std::string& json, const std::string& key)
{
    const auto marker = "\"" + key + "\":\"";
    const auto start = json.find(marker);
    if (start == std::string::npos)
        return {};
    const auto valueStart = start + marker.size();
    const auto end = json.find('"', valueStart);
    return end == std::string::npos ? std::string{} : json.substr(valueStart, end - valueStart);
}

void writeFragmented(const std::string& line)
{
    const auto split = line.size() / 2;
    std::cout.write(line.data(), static_cast<std::streamsize>(split));
    std::cout.flush();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    std::cout.write(line.data() + split, static_cast<std::streamsize>(line.size() - split));
    std::cout.flush();
}
}

int main()
{
    std::string request;
    if (!std::getline(std::cin, request))
        return 2;
    const std::string id = value(request, "id");
    const std::string operation = value(request, "operation");
    std::cerr << "fake diagnostic\n";
    std::cerr.flush();

    if (operation == "capabilities") {
        const bool incompatible = request.find("\"loader_protocol\":2") != std::string::npos;
        const std::string protocol = incompatible ? "2" : "1";
        writeFragmented("{\"protocol\":1,\"type\":\"result\",\"id\":\"" + id + "\",\"result\":{\"protocol\":1,\"loader_protocol\":" + protocol + "}}\n");
        return 0;
    }

    if (operation == "structured-error") {
        writeFragmented("{\"protocol\":1,\"type\":\"result\",\"id\":\"" + id + "\",\"error\":{\"code\":\"network\",\"message\":\"offline\"}}\n");
        return 0;
    }

    writeFragmented("{\"protocol\":1,\"type\":\"progress\",\"id\":\"" + id + "\",\"message\":\"scan started\"}\n");
    if (operation == "cancel-test") {
        std::string cancel;
        if (!std::getline(std::cin, cancel) || value(cancel, "type") != "cancel")
            return 3;
        writeFragmented("{\"protocol\":1,\"type\":\"result\",\"id\":\"" + id + "\",\"error\":{\"code\":\"cancelled\",\"message\":\"cancelled cooperatively\"}}\n");
        return 0;
    }

    if (operation == "delayed-cancel-test") {
        std::string cancel;
        if (!std::getline(std::cin, cancel) || value(cancel, "type") != "cancel")
            return 3;
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        writeFragmented("{\"protocol\":1,\"type\":\"result\",\"id\":\"" + id + "\",\"error\":{\"code\":\"cancelled\",\"message\":\"cancelled cooperatively\"}}\n");
        return 0;
    }

    if (operation == "recovery-cancel-test") {
        std::string cancel;
        if (!std::getline(std::cin, cancel) || value(cancel, "type") != "cancel")
            return 3;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        writeFragmented("{\"protocol\":1,\"type\":\"result\",\"id\":\"" + id + "\",\"error\":{\"code\":\"recovery_failed\",\"message\":\"rollback failed; backups preserved at /staging/mods/backup\"}}\n");
        return 0;
    }

    if (operation == "terminal-delay-test") {
        writeFragmented("{\"protocol\":1,\"type\":\"result\",\"id\":\"" + id + "\",\"result\":{\"ok\":true}}\n");
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        return 0;
    }

    writeFragmented("{\"protocol\":1,\"type\":\"result\",\"id\":\"" + id + "\",\"result\":{\"ok\":true}}\n");
    return 0;
}
