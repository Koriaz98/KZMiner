#include "work_client.h"
#include "../console_lock.h"
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <iostream>
#include <chrono>

using json = nlohmann::json;

namespace
{
    size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp)
    {
        size_t totalSize = size * nmemb;
        std::string* str = static_cast<std::string*>(userp);
        str->append(static_cast<char*>(contents), totalSize);
        return totalSize;
    }

    std::vector<uint8_t> hexToBytes(const std::string& hex)
    {
        std::vector<uint8_t> bytes;
        bytes.reserve(hex.size() / 2);
        for(size_t i = 0; i + 1 < hex.size(); i += 2)
        {
            uint8_t byte = static_cast<uint8_t>(
                std::stoul(hex.substr(i, 2), nullptr, 16)
            );
            bytes.push_back(byte);
        }
        return bytes;
    }
}

WorkClient::WorkClient(
    const std::string& poolUrl,
    const std::string& address,
    const std::string& worker
)
: poolUrl_(poolUrl), address_(address), worker_(worker)
{
}

std::string WorkClient::httpPost(
    const std::string& path,
    const std::string& jsonBody
)
{
    CURL* curl = curl_easy_init();
    if(!curl)
    {
        std::lock_guard<std::mutex> lock(consoleMutex());
        std::cerr << "WorkClient: curl_easy_init failed\n";
        return "";
    }

    std::string url = poolUrl_ + path;
    std::string response;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonBody.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl);

    long httpStatus = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpStatus);
    lastHttpStatus_ = httpStatus;

    if(res != CURLE_OK)
    {
        std::lock_guard<std::mutex> lock(consoleMutex());
        std::cerr << "WorkClient: HTTP request failed: " << curl_easy_strerror(res) << "\n";
        response.clear();
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return response;
}

std::optional<MiningWork> WorkClient::requestWork()
{
    json req;
    req["address"] = address_;
    if(!worker_.empty())
    {
        req["worker"] = worker_;
    }

    std::string response = httpPost("/api/v1/work", req.dump());
    if(response.empty())
    {
        return std::nullopt;
    }

    json j;
    try
    {
        j = json::parse(response);
    }
    catch(...)
    {
        std::string preview = response.substr(0, 200);
        std::lock_guard<std::mutex> lock(consoleMutex());
        std::cerr << "WorkClient: invalid JSON in work response (length="
                   << response.size() << "): \"" << preview << "\"\n";
        return std::nullopt;
    }

    if(j.contains("error_code"))
    {
        std::lock_guard<std::mutex> lock(consoleMutex());
        std::cerr << "WorkClient: work error: " << j["error_code"].get<std::string>() << "\n";
        return std::nullopt;
    }

    if(!j.contains("schema_version") || j["schema_version"].get<int>() != 1)
    {
        std::lock_guard<std::mutex> lock(consoleMutex());
        std::cerr << "WorkClient: unexpected schema_version\n";
        return std::nullopt;
    }

    if(!j.contains("network") || j["network"].get<std::string>() != "btc09-mainnet")
    {
        std::lock_guard<std::mutex> lock(consoleMutex());
        std::cerr << "WorkClient: unexpected network field\n";
        return std::nullopt;
    }

    MiningWork work;
    work.job_id        = j.value("job_id", "");
    work.height         = j.value("height", 0ULL);
    work.expires_at      = j.value("expires_at", "");
    work.argon_mem_kib  = j.value("argon_mem_kib", 0U);
    work.argon_time     = j.value("argon_time", 0U);

    std::string headerHex = j.value("header_hex", "");
    std::string targetHex = j.value("target_hex", "");

    if(work.job_id.size() != 32)
    {
        std::lock_guard<std::mutex> lock(consoleMutex());
        std::cerr << "WorkClient: job_id has unexpected length\n";
        return std::nullopt;
    }

    if(headerHex.size() != 176)
    {
        std::lock_guard<std::mutex> lock(consoleMutex());
        std::cerr << "WorkClient: header_hex has unexpected length\n";
        return std::nullopt;
    }

    if(targetHex.size() != 64)
    {
        std::lock_guard<std::mutex> lock(consoleMutex());
        std::cerr << "WorkClient: target_hex has unexpected length\n";
        return std::nullopt;
    }

    work.header = hexToBytes(headerHex);
    work.target = hexToBytes(targetHex);

    // Le nonce (8 derniers octets) doit etre a zero dans le travail recu
    bool nonceIsZero = true;
    for(size_t i = work.header.size() - 8; i < work.header.size(); i++)
    {
        if(work.header[i] != 0)
        {
            nonceIsZero = false;
            break;
        }
    }

    if(!nonceIsZero)
    {
        std::lock_guard<std::mutex> lock(consoleMutex());
        std::cerr << "WorkClient: header nonce is not zero, rejecting work\n";
        return std::nullopt;
    }

    return work;
}

SubmitResult WorkClient::submitNonce(
    const std::string& job_id,
    uint64_t nonce
)
{
    json req;
    req["job_id"] = job_id;
    req["nonce"] = nonce;

    std::string response = httpPost("/api/v1/submit", req.dump());

    SubmitResult result;

    if(response.empty())
    {
        result.error_code = "network_error";
        return result;
    }

    json j;
    try
    {
        j = json::parse(response);
    }
    catch(...)
    {
        result.error_code = "invalid_response";
        return result;
    }

    if(j.contains("error_code"))
    {
        result.error_code = j["error_code"].get<std::string>();
        return result;
    }

    result.status = j.value("status", "");
    result.block_id = j.value("block_id", "");
    result.ok = (result.status == "block_accepted");

    return result;
}
