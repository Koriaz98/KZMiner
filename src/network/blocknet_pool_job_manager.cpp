#include "blocknet_pool_job_manager.h"
#include "../console_output.h"
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

BlocknetPoolJobManager::BlocknetPoolJobManager(
    const std::string& host,
    int port,
    const std::string& wallet,
    const std::string& worker
)
: host_(host), port_(port), wallet_(wallet), worker_(worker)
, client_(std::make_unique<BlocknetPoolClient>(host, port, wallet, worker))
{
}

BlocknetPoolJobManager::~BlocknetPoolJobManager()
{
    running_ = false;
    client_->stop();
    if(netThread_.joinable()) netThread_.join();
    if(watchdogThread_.joinable()) watchdogThread_.join();
}

int BlocknetPoolJobManager::reconnectDelaySeconds() const
{
    return 60;
}

void BlocknetPoolJobManager::watchdogLoop()
{
    while(running_)
    {
        if(netThread_.joinable())
        {
            netThread_.join();
        }
        if(!running_) break;

        if(client_->hadSuccessfulSession())
        {
            consecutiveFailures_ = 0;
        }
        else
        {
            if(consecutiveFailures_ < 10) consecutiveFailures_++;
        }

        int delay = reconnectDelaySeconds();

        pushLogLine("[blocknet] connection lost, reconnecting in " + std::to_string(delay) + "s...");

        std::this_thread::sleep_for(std::chrono::seconds(delay));
        if(!running_) break;

        client_ = std::make_unique<BlocknetPoolClient>(host_, port_, wallet_, worker_);
        if(client_->connect())
        {
            netThread_ = std::thread(&BlocknetPoolClient::run, client_.get());
        }
        else
        {
            if(consecutiveFailures_ < 10) consecutiveFailures_++;
            int nextDelay = reconnectDelaySeconds();
            pushLogLine("[blocknet] reconnection failed, retrying in " + std::to_string(nextDelay) + "s");
            netThread_ = std::thread([](){});
        }
    }
}

void BlocknetPoolJobManager::start()
{
    running_ = true;
    if(!client_->connect())
    {
        if(consecutiveFailures_ < 10) consecutiveFailures_++;
        pushLogLine("[blocknet] initial connection failed");
    }
    netThread_ = std::thread(&BlocknetPoolClient::run, client_.get());
    watchdogThread_ = std::thread(&BlocknetPoolJobManager::watchdogLoop, this);
}

MiningJob BlocknetPoolJobManager::getJob()
{
    BlocknetPoolJob pj = client_->getJob();
    MiningJob job;
    job.valid         = pj.valid;
    job.job_id        = pj.job_id;
    job.height        = pj.height;
    job.header        = hexToBytes(pj.header_base_hex);
    job.target        = hexToBytes(pj.target_hex);
    job.difficulty    = pj.difficulty;
    job.nonce_start   = pj.nonce_start;
    job.nonce_end     = pj.nonce_end;
    // Parametres Argon2id de Blocknet (2 GiB, 1 iteration) - fixes,
    // le protocole ne les annonce pas job par job comme le fait BTC09.
    job.argon_mem_kib = 2u * 1024u * 1024u;
    job.argon_time    = 1;
    return job;
}

void BlocknetPoolJobManager::submitNonce(
    const std::string& job_id,
    uint64_t nonce,
    const std::vector<uint8_t>& hash,
    uint64_t /*height*/,
    bool /*isDevFeeJob*/
)
{
    // Le pool officiel bntpool.com exige la capacite
    // "submit_claimed_hash" (confirme par un rejet reel de connexion
    // sans cette declaration) - on transmet donc bien notre propre
    // hash calcule, contrairement au comportement solo/pool BTC09 ou
    // le coordinateur/pool reconstruit tout lui-meme sans avoir besoin
    // de notre resultat.
    client_->submit(job_id, nonce, bytesToHex(hash));
}

uint64_t BlocknetPoolJobManager::getAcceptedCount() const
{
    return client_->getAcceptedCount();
}

uint64_t BlocknetPoolJobManager::getRejectedCount() const
{
    return client_->getRejectedCount();
}
