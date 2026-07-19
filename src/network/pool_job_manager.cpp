#include "pool_job_manager.h"
#include "../console_output.h"
#include <sstream>
#include <iomanip>
#include <chrono>
#include <algorithm>

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

int PoolJobManager::reconnectDelaySeconds() const
{
    // Delai fixe : une tentative de reconnexion toutes les 60s en cas
    // de deconnexion, jamais plus rapproche.
    return 60;
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

        // Evalue le VRAI resultat de la session qui vient de se
        // terminer (login confirme ou job recu), pas juste si la
        // poignee de main TCP initiale avait reussi - certains pools
        // acceptent le TCP puis coupent la session peu apres, ce qui
        // ferait sinon repartir le compteur d'echecs a zero a chaque
        // cycle sans jamais laisser le delai progresser.
        if(client_->hadSuccessfulSession())
        {
            consecutiveFailures_ = 0;
        }
        else
        {
            if(consecutiveFailures_ < 10) consecutiveFailures_++;
        }

        int delay = reconnectDelaySeconds();

        pushLogLine("[pool] connection lost, reconnecting in " + std::to_string(delay) + "s...");

        std::this_thread::sleep_for(std::chrono::seconds(delay));
        if(!running_) break;

        client_ = std::make_unique<PoolClient>(host_, port_, wallet_, worker_);
        if(client_->connect())
        {
            netThread_ = std::thread(&PoolClient::run, client_.get());
        }
        else
        {
            if(consecutiveFailures_ < 10) consecutiveFailures_++;
            int nextDelay = reconnectDelaySeconds();
            pushLogLine("[pool] reconnection failed, retrying in " + std::to_string(nextDelay) + "s");
            netThread_ = std::thread([](){});
        }
    }
}

void PoolJobManager::start()
{
    running_ = true;
    if(!client_->connect())
    {
        if(consecutiveFailures_ < 10) consecutiveFailures_++;
        pushLogLine("[pool] initial connection failed");
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
    uint64_t /*height*/,
    bool /*isDevFeeJob*/
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
