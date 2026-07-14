#include "update_check.h"
#include "version.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <string>
#include <sstream>
#include <vector>

namespace
{
    size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp)
    {
        size_t totalSize = size * nmemb;
        std::string* str = static_cast<std::string*>(userp);
        str->append(static_cast<char*>(contents), totalSize);
        return totalSize;
    }

    // Parse "v1.2.3" ou "v1.2.3-4-gabcdef-dirty" -> {1, 2, 3}.
    // Retourne false si le format n'est pas exploitable (ex: "dev").
    bool parseVersion(const std::string& raw, std::vector<int>& out)
    {
        std::string s = raw;
        if(!s.empty() && s[0] == 'v')
        {
            s = s.substr(1);
        }

        // Coupe tout ce qui suit le premier '-' (suffixe git describe)
        auto dashPos = s.find('-');
        if(dashPos != std::string::npos)
        {
            s = s.substr(0, dashPos);
        }

        out.clear();
        std::stringstream ss(s);
        std::string part;
        while(std::getline(ss, part, '.'))
        {
            try
            {
                out.push_back(std::stoi(part));
            }
            catch(...)
            {
                return false;
            }
        }

        return !out.empty();
    }

    // Retourne true si "latest" est strictement plus recent que "current".
    bool isNewer(const std::vector<int>& latest, const std::vector<int>& current)
    {
        size_t n = std::max(latest.size(), current.size());
        for(size_t i = 0; i < n; i++)
        {
            int l = (i < latest.size()) ? latest[i] : 0;
            int c = (i < current.size()) ? current[i] : 0;
            if(l != c)
            {
                return l > c;
            }
        }
        return false;
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

        std::string latestStr = j["tag_name"].get<std::string>();
        std::string currentStr = KZMinerInfo::kVersion;

        std::vector<int> latestParsed, currentParsed;
        bool latestOk = parseVersion(latestStr, latestParsed);
        bool currentOk = parseVersion(currentStr, currentParsed);

        // Si l'une des deux versions n'est pas parseable (ex: build "dev"),
        // on ne peut pas comparer numeriquement -> on ne dit rien plutot
        // que de risquer un faux avertissement.
        if(!latestOk || !currentOk)
        {
            return;
        }

        if(isNewer(latestParsed, currentParsed))
        {
            std::cout
                << "\n\033[33m[!] New version available: " << latestStr
                << " (current: " << currentStr << ")\n"
                << "    To check the latest version of KZMiner, go to https://github.com/"
                << KZMinerInfo::kRepo << "/releases/latest\033[0m\n\n";
        }
    }
    catch(...)
    {
    }
}
