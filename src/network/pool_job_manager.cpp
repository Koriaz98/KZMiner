#include "pool_job_manager.h"
#include "../console_lock.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>

namespace
{
    std::vector<uint8_t> hexToBytes(const std::string& hex)
    {
        std::vector<uint8_t> bytes;
        bytes.reserve(hex.size() / 2);
        for(size_t i = 0; i + 1 < hex.size(); i += 2)
        {
            uint8_t b = static_cast<uint8_t>(
                std::stoul(hex.substr(i, 2), nullptr, 16)
            );
            bytes.push_back(b);
        }
        return bytes;
    }

    std::string bytesToHex(const std::vector<uint8_t>& bytes)
    {
        std::ostringstream oss;
        for(uint8_t b : bytes)
        {
            oss << std::hex << std::setw(2) << std::setfill('0') << (int)b;
        }
        return oss.str();
    }
}

PoolJobManager::PoolJobManager(
    const std::string& host,
    int port,
    const std::string& wallet,
    const std::string& worker
)
: host_(host), port_(port), wallet_(wallet), worker_(worker)
, client_(std::make_unique<PoolClient>(host, port, wallet, worker))
{
}

PoolJobManager::~PoolJobManager()
{
    running_ = false;
    client_->stop();
    if(netThread_.joinable()) netThread_.join();
    if(watchdogThread_.joinable()) watchdogThread_.join();
}

void PoolJobManager::watchdogLoop()
{
    while(running_)
    {
        if(netThread_.joinable())
        {
            netThread_.join();
        }
        if(!running_) break;

        {
            std::lock_guard<std::mutex> lock(consoleMutex());
            std::cerr << "PoolJobManager (" << host_ << ":" << port_
                       << "): connection lost, reconnecting in 5s...\n";
        }

        std::this_thread::sleep_for(std::chrono::seconds(5));
        if(!running_) break;

        client_ = std::make_unique<PoolClient>(host_, port_, wallet_, worker_);
        if(client_->connect())
        {
            netThread_ = std::thread(&PoolClient::run, client_.get());
        }
        else
        {
            std::lock_guard<std::mutex> lock(consoleMutex());
            std::cerr << "PoolJobManager (" << host_ << ":" << port_
                       << "): reconnection failed, retrying in 5s\n";
            netThread_ = std::thread([](){});
        }
    }
}

void PoolJobManager::start()
{
    running_ = true;
    if(!client_->connect())
    {
        std::lock_guard<std::mutex> lock(consoleMutex());
        std::cerr << "PoolJobManager: initial connection failed\n";
    }
    netThread_ = std::thread(&PoolClient::run, client_.get());
    watchdogThread_ = std::thread(&PoolJobManager::watchdogLoop, this);
}

MiningJob PoolJobManager::getJob()
{
    PoolJob pj = client_->getJob();
    MiningJob job;
    job.valid         = pj.valid;
    job.job_id         = pj.job_id;
    job.height         = pj.height;
    job.header         = hexToBytes(pj.header_hex);
    job.target         = hexToBytes(pj.target_hex);
    job.difficulty     = pj.difficulty;
    job.nonce_start    = pj.nonce_start;
    job.nonce_end      = pj.nonce_end;
    job.argon_mem_kib  = 65536;
    job.argon_time     = 1;
    return job;
}

void PoolJobManager::submitNonce(
    const std::string& job_id,
    uint64_t nonce,
    const std::vector<uint8_t>& hash,
    uint64_t /*height*/
)
{
    client_->submit(job_id, nonce, bytesToHex(hash));
}

uint64_t PoolJobManager::getAcceptedCount() const
{
    return client_->getAcceptedCount();
}

uint64_t PoolJobManager::getRejectedCount() const
{
    return client_->getRejectedCount();
}
