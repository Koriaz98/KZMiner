#include "solo_job_manager.h"
#include "../console_output.h"
#include <sstream>
#include <chrono>
#include <cstdio>

namespace
{
    // MaxTargetBits mainnet officiel (core/params.go: 0x1f00ffff),
    // target de reference pour difficulty = 1, en 32 octets big-endian.
    const std::vector<uint8_t> kMaxTarget = {
        0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    double bytesToDouble(const std::vector<uint8_t>& bytes)
    {
        double result = 0.0;
        for(uint8_t b : bytes)
        {
            result = result * 256.0 + static_cast<double>(b);
        }
        return result;
    }

    double computeDifficulty(const std::vector<uint8_t>& target)
    {
        if(target.size() != 32) return 0.0;
        double t = bytesToDouble(target);
        if(t <= 0.0) return 0.0;
        return bytesToDouble(kMaxTarget) / t;
    }
}

SoloJobManager::SoloJobManager(
    const std::string& poolUrl,
    const std::string& address,
    const std::string& worker,
    int pollIntervalSeconds
)
: worker_(worker)
, pollIntervalSeconds_(pollIntervalSeconds)
, client_(poolUrl, address, worker)
{
}

SoloJobManager::~SoloJobManager()
{
    running_ = false;
    if(pollThread_.joinable())
    {
        pollThread_.join();
    }
}

std::string SoloJobManager::sourceLabel() const
{
    // Le worker du dev fee est toujours suffixe par "-devfee" (voir
    // DevFeeSource::start()) - permet de distinguer dans les logs si
    // un message vient de la connexion wallet utilisateur ou dev fee,
    // les deux tournant en parallele et independamment.
    static const std::string kSuffix = "-devfee";
    bool isDevFee = worker_.size() >= kSuffix.size()
        && worker_.compare(worker_.size() - kSuffix.size(), kSuffix.size(), kSuffix) == 0;
    return isDevFee ? "[solo:devfee]" : "[solo:user]";
}

bool SoloJobManager::refreshWork()
{
    auto work = client_.requestWork();
    if(!work.has_value())
    {
        bool rateLimited = (client_.getLastHttpStatus() == 429);
        if(rateLimited)
        {
            pushLogLine(sourceLabel() + " rate limited, retrying in 90s");
        }
        else
        {
            pushLogLine(sourceLabel() + " couldn't fetch work, retrying");
        }
        return rateLimited;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    bool isNewJob = (work->job_id != current_.job_id);
    current_.valid         = true;
    current_.job_id         = work->job_id;
    current_.height         = work->height;
    current_.header         = work->header;
    current_.target         = work->target;
    current_.argon_mem_kib = work->argon_mem_kib;
    current_.argon_time     = work->argon_time;
    current_.difficulty     = computeDifficulty(work->target);
    current_.nonce_start    = 0;
    current_.nonce_end      = 0; // signal "mode solo, pas de plage imposee"

    if(isNewJob)
    {
        std::string shortId = current_.job_id.substr(0, 16);
        std::ostringstream oss;
        oss << sourceLabel() << " new job " << shortId
            << ", height " << current_.height
            << ", difficulty " << current_.difficulty;
        pushLogLine(oss.str());
    }

    return false;
}

void SoloJobManager::pollLoop()
{
    while(running_)
    {
        bool rateLimited = refreshWork();

        // Cycle normal : 10s (100 x 100ms). En cas de 429, on patiente
        // beaucoup plus longtemps (90s) avant de reessayer, pour
        // laisser le temps a la limite de debit du coordinateur de se
        // reinitialiser plutot que d'insister au meme rythme.
        int waitSteps = rateLimited ? 900 : (pollIntervalSeconds_ * 10);
        for(int i = 0; i < waitSteps && running_; i++)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void SoloJobManager::start()
{
    running_ = true;
    for(int attempt = 0; attempt < 5; attempt++)
    {
        bool rateLimited = refreshWork();
        if(current_.valid) break;

        int waitSeconds = rateLimited ? 90 : 2;
        pushLogLine(sourceLabel() + " retrying initial connection in " + std::to_string(waitSeconds) + "s...");
        std::this_thread::sleep_for(std::chrono::seconds(waitSeconds));
    }
    pollThread_ = std::thread(&SoloJobManager::pollLoop, this);
}

MiningJob SoloJobManager::getJob()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return current_;
}

void SoloJobManager::submitNonce(
    const std::string& job_id,
    uint64_t nonce,
    const std::vector<uint8_t>& /*hash*/,
    uint64_t height,
    bool /*isDevFeeJob*/
)
{
    auto result = client_.submitNonce(job_id, nonce);

    if(result.ok)
    {
        acceptedCount_++;
    }
    else
    {
        rejectedCount_++;
    }

    if(result.ok)
    {
        std::ostringstream oss;
        oss << "\033[1;32m*** BLOCK FOUND (solo) *** height=" << height
            << " block_id=" << result.block_id << "\033[0m";
        pushLogLine(oss.str());
    }
    else
    {
        pushLogLine(sourceLabel() + " submission rejected: " + result.error_code);
    }
}
