#include "update_check.h"
#include "version.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <string>

namespace
{
    size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp)
    {
        size_t totalSize = size * nmemb;
        std::string* str = static_cast<std::string*>(userp);
        str->append(static_cast<char*>(contents), totalSize);
        return totalSize;
    }
}

void checkForUpdate()
{
    CURL* curl = curl_easy_init();
    if(!curl) return;

    std::string response;
    std::string url =
        std::string("https://api.github.com/repos/") + KZMinerInfo::kRepo + "/releases/latest";

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "User-Agent: KZMiner");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 3L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if(res != CURLE_OK || response.empty())
    {
        return;
    }

    try
    {
        auto j = nlohmann::json::parse(response);
        if(!j.contains("tag_name")) return;

        std::string latest = j["tag_name"].get<std::string>();
        std::string current = KZMinerInfo::kVersion;

        if(!latest.empty() && latest != current)
        {
            std::cout
                << "\n\033[33m[!] New version available: " << latest
                << " (current: " << current << ")\n"
                << "    Download: https://github.com/" << KZMinerInfo::kRepo << "/releases/latest\033[0m\n\n";
        }
    }
    catch(...)
    {
    }
}
