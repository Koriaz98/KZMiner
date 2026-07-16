#include "devfee_source.h"
#include <iostream>
#include <cmath>
#include "../console_lock.h"

DevFeeSource::DevFeeSource(
    std::unique_ptr<MiningSource> userSource,
    std::unique_ptr<MiningSource> devSource,
    double feePercent,
    int cycleSeconds
)
: userSource_(std::move(userSource))
, devSource_(std::move(devSource))
, feePercent_(feePercent)
, cycleSeconds_(cycleSeconds)
{
}

void DevFeeSource::start()
{
    startTime_ = std::chrono::steady_clock::now();
    userSource_->start();
    devSource_->start();

    {
        std::lock_guard<std::mutex> lock(consoleMutex());
        std::cout
            << "Dev fee: " << feePercent_ << "% ("
            << (cycleSeconds_ * feePercent_ / 100.0)
            << "s every " << cycleSeconds_ << "s)\n";
    }
}

bool DevFeeSource::isDevActive()
{
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - startTime_).count();
    double phase = std::fmod(elapsed, static_cast<double>(cycleSeconds_));
    double devSlice = cycleSeconds_ * (feePercent_ / 100.0);
    return phase < devSlice;
}

MiningJob DevFeeSource::getJob()
{
    bool devActive = isDevActive();

    bool wasDev = lastActiveWasDev_.exchange(devActive);
    if(devActive != wasDev)
    {
        std::lock_guard<std::mutex> lock(consoleMutex());
        std::cout
            << (devActive
                ? "[devfee] now mining for the developer wallet (1% fee, non-refundable)\n"
                : "[devfee] resumed mining for your wallet\n");
    }

    return devActive ? devSource_->getJob() : userSource_->getJob();
}

void DevFeeSource::submitNonce(
    const std::string& job_id,
    uint64_t nonce,
    const std::vector<uint8_t>& hash,
    uint64_t height
)
{
    if(isDevActive())
    {
        devSource_->submitNonce(job_id, nonce, hash, height);
    }
    else
    {
        userSource_->submitNonce(job_id, nonce, hash, height);
    }
}
