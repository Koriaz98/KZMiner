#include "pool_client.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

using json = nlohmann::json;

PoolClient::PoolClient(
    const std::string& host,
    int port,
    const std::string& wallet,
    const std::string& worker
)
: host_(host), port_(port), wallet_(wallet), worker_(worker)
{
}

PoolClient::~PoolClient()
{
    if(sock_ >= 0)
    {
        close(sock_);
        sock_ = -1;
    }
}

bool PoolClient::connect()
{
    struct addrinfo hints{};
    struct addrinfo* res = nullptr;

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    std::string portStr = std::to_string(port_);

    if(getaddrinfo(host_.c_str(), portStr.c_str(), &hints, &res) != 0)
    {
        std::cerr << "PoolClient: DNS resolution failed for " << host_ << "\n";
        return false;
    }

    sock_ = socket(AF_INET, SOCK_STREAM, 0);
    if(sock_ < 0)
    {
        std::cerr << "PoolClient: socket() failed\n";
        freeaddrinfo(res);
        return false;
    }

    if(::connect(sock_, res->ai_addr, res->ai_addrlen) != 0)
    {
        std::cerr << "PoolClient: connect() failed to " << host_ << ":" << port_ << "\n";
        freeaddrinfo(res);
        close(sock_);
        sock_ = -1;
        return false;
    }

    freeaddrinfo(res);

    std::cout << "PoolClient: connected to " << host_ << ":" << port_ << "\n";

    json login;
    login["id"] = 1;
    login["method"] = "login";
    login["params"]["address"] = wallet_;
    login["params"]["worker"] = worker_;
    sendJson(login.dump());

    return true;
}

void PoolClient::sendJson(const std::string& payload)
{
    std::string line = payload + "\n";
    ssize_t sent = send(sock_, line.c_str(), line.size(), MSG_NOSIGNAL);
    if(sent < 0)
    {
        std::cerr << "PoolClient: send() failed\n";
    }
}

void PoolClient::run()
{
    running_ = true;
    std::string buffer;
    char chunk[8192];

    while(running_)
    {
        ssize_t n = recv(sock_, chunk, sizeof(chunk) - 1, 0);
        if(n <= 0)
        {
            std::cerr << "PoolClient: connection closed or error\n";
            break;
        }

        chunk[n] = '\0';
        buffer += chunk;

        size_t pos;
        while((pos = buffer.find('\n')) != std::string::npos)
        {
            std::string line = buffer.substr(0, pos);
            buffer.erase(0, pos + 1);
            if(!line.empty())
            {
                handleLine(line);
            }
        }
    }

    running_ = false;
}

void PoolClient::handleLine(const std::string& line)
{
    json msg;
    try
    {
        msg = json::parse(line);
    }
    catch(...)
    {
        std::cerr << "PoolClient: failed to parse line: " << line << "\n";
        return;
    }

    if(msg.contains("id") && msg["id"].is_number() &&
       msg["id"].get<long long>() == 1 && msg.contains("result"))
    {
        try
        {
            std::string status = msg["result"]["status"].get<std::string>();
            if(status == "ok")
            {
                std::cout << "PoolClient: login accepted by pool\n";
            }
        }
        catch(...) {}
        return;
    }

    if(msg.contains("result") && msg["result"].is_object() &&
       msg["result"].contains("status"))
    {
        std::string status = msg["result"]["status"].get<std::string>();
        if(status == "accepted")
        {
            acceptedCount_++;
        }
        else
        {
            rejectedCount_++;
        }
        return;
    }

    if(msg.contains("method") && msg["method"] == "job" && msg.contains("params"))
    {
        auto& p = msg["params"];

        std::lock_guard<std::mutex> lock(jobMutex_);
        currentJob_.job_id      = p.value("job_id", "");
        currentJob_.height      = p.value("height", 0ULL);
        currentJob_.header_hex   = p.value("header", "");
        currentJob_.target_hex   = p.value("target", "");
        currentJob_.difficulty  = p.value("difficulty", 0.0);
        currentJob_.nonce_start = p.value("nonce_start", 0ULL);
        currentJob_.nonce_end   = p.value("nonce_end", 0ULL);
        currentJob_.valid       = true;

        std::cout
            << "[pool] job " << currentJob_.job_id
            << " height=" << currentJob_.height
            << " difficulty=" << currentJob_.difficulty
            << "\n";
    }
}

PoolJob PoolClient::getJob()
{
    std::lock_guard<std::mutex> lock(jobMutex_);
    return currentJob_;
}

void PoolClient::submit(
    const std::string& job_id,
    uint64_t nonce,
    const std::string& hashHex
)
{
    json sub;
    sub["id"] = requestId_++;
    sub["method"] = "submit";
    sub["params"]["job_id"] = job_id;
    sub["params"]["nonce"] = nonce;
    sub["params"]["hash"] = hashHex;
    sendJson(sub.dump());
}

void PoolClient::stop()
{
    running_ = false;
    if(sock_ >= 0)
    {
        shutdown(sock_, SHUT_RDWR);
        close(sock_);
        sock_ = -1;
    }
}
