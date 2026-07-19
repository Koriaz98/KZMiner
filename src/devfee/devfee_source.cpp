#include "devfee_source.h"
#include <sstream>
#include <cmath>
#include <thread>
#include "../console_output.h"

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

    // Demarrage decale du wallet dev fee (35s apres le wallet
    // utilisateur), pour eviter deux tentatives de connexion quasi
    // simultanees depuis la meme IP a chaque (re)lancement - et, par
    // effet de bord, les cycles de reconnexion des deux sources
    // restent ensuite decales dans le temps plutot que groupes.
    MiningSource* devSourceRaw = devSource_.get();
    std::thread([devSourceRaw]()
    {
        std::this_thread::sleep_for(std::chrono::seconds(35));
        devSourceRaw->start();
    }).detach();

    std::ostringstream oss;
    oss << "Dev fee: " << feePercent_ << "% ("
        << (cycleSeconds_ * feePercent_ / 100.0)
        << "s every " << cycleSeconds_ << "s, dev wallet connects 35s after startup)";
    pushLogLine(oss.str());
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
        pushLogLine(devActive
            ? "[devfee] now mining for the developer wallet (1% fee, non-refundable)"
            : "[devfee] resumed mining for your wallet");
    }

    MiningJob job = devActive ? devSource_->getJob() : userSource_->getJob();

    // Fige dans le job lui-meme quelle source a ete choisie A CET
    // INSTANT precis - submitNonce() reutilisera cette decision telle
    // quelle, plutot que de reevaluer isDevActive() a un instant
    // different (potentiellement apres que la fenetre de 1% ait
    // bascule), ce qui pourrait autrement envoyer un resultat calcule
    // avec le job utilisateur vers la connexion dev fee, ou l'inverse.
    job.isDevFeeJob = devActive;

    return job;
}

void DevFeeSource::submitNonce(
    const std::string& job_id,
    uint64_t nonce,
    const std::vector<uint8_t>& hash,
    uint64_t height,
    bool isDevFeeJob
)
{
    if(isDevFeeJob)
    {
        devSource_->submitNonce(job_id, nonce, hash, height, isDevFeeJob);
    }
    else
    {
        userSource_->submitNonce(job_id, nonce, hash, height, isDevFeeJob);
    }
}
