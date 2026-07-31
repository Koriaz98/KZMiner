#include "pool_job_manager.h"
#include "../console_output.h"
#include <sstream>
#include <iomanip>
#include <chrono>
#include <algorithm>
#include <cstdlib>

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
, client_(std::make_shared<PoolClient>(host, port, wallet, worker))
{
}

std::shared_ptr<PoolClient> PoolJobManager::snapshotClient() const
{
    std::lock_guard<std::mutex> lk(clientMutex_);
    return client_;
}

PoolJobManager::~PoolJobManager()
{
    running_ = false;
    {
        // Stoppe le client courant SOUS le lock : ferme son socket, donc
        // son run() sort, ce qui debloque le netThread_.join() interne du
        // watchdog. Le recheck running_ dans watchdogLoop() garantit
        // qu'aucun nouveau client ne sera publie apres ce point.
        std::lock_guard<std::mutex> lk(clientMutex_);
        if(client_) client_->stop();
    }
    // Join le watchdog EN PREMIER : quand ce join retourne, watchdogLoop()
    // a entierement termine et ne touchera plus netThread_. Le join de
    // netThread_ qui suit est donc sans concurrence (voir issue #1).
    if(watchdogThread_.joinable()) watchdogThread_.join();
    if(netThread_.joinable()) netThread_.join();
}

int PoolJobManager::reconnectDelaySeconds() const
{
    // Delai fixe : une tentative de reconnexion toutes les 60s en cas
    // de deconnexion, jamais plus rapproche. Surchargeable via la
    // variable d'environnement KZMINER_RECONNECT_DELAY_SECONDS UNIQUEMENT
    // pour les tests (ex: 0 pour forcer des reconnexions rapides sous
    // ThreadSanitizer). Absente en prod -> comportement inchange.
    if(const char* env = std::getenv("KZMINER_RECONNECT_DELAY_SECONDS"))
    {
        return std::atoi(env);
    }
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
        {
            // Lecture d'etat de l'ancien client via snapshot (le watchdog
            // pourrait sinon lire client_ pendant qu'il le remplace).
            auto old = snapshotClient();
            if(old && old->hadSuccessfulSession())
            {
                consecutiveFailures_ = 0;
            }
            else
            {
                if(consecutiveFailures_ < 10) consecutiveFailures_++;
            }
        }

        int delay = reconnectDelaySeconds();

        pushLogLine("[pool] connection lost, reconnecting in " + std::to_string(delay) + "s...");

        std::this_thread::sleep_for(std::chrono::seconds(delay));
        if(!running_) break;

        // Client neuf construit + connecte EN LOCAL (invisible des workers) ;
        // connect() bloquant tenu HORS du lock.
        auto newClient = std::make_shared<PoolClient>(host_, port_, wallet_, worker_);
        bool ok = newClient->connect();
        {
            std::lock_guard<std::mutex> lk(clientMutex_);
            if(!running_) { newClient->stop(); break; }  // teardown en cours : ne pas publier
            // Replie les comptes du client sortant (deja quiescent : son
            // run() a ete joint en tete de boucle) dans la base, sous le
            // MEME lock que getAccepted/RejectedCount() -> le swap est
            // tout-ou-rien pour les lecteurs, pas de double comptage.
            if(client_)
            {
                acceptedBase_ += client_->getAcceptedCount();
                rejectedBase_ += client_->getRejectedCount();
            }
            client_ = newClient;                         // publication atomique sous lock
        }
        if(ok)
        {
            // Capture du shared_ptr par valeur : garde le client vivant
            // pendant toute la duree de son run().
            netThread_ = std::thread([newClient](){ newClient->run(); });
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
    // start() s'execute avant tout worker (voir main.cpp : source->start()
    // precede launchWorkers()) et avant le spawn du watchdog ci-dessous :
    // pas de concurrence sur client_ ici, mais on passe par snapshot par
    // uniformite. connect() bloquant tenu hors du lock (client local).
    auto c = snapshotClient();
    if(!c || !c->connect())
    {
        if(consecutiveFailures_ < 10) consecutiveFailures_++;
        pushLogLine("[pool] initial connection failed");
    }
    if(c) netThread_ = std::thread([c](){ c->run(); });
    watchdogThread_ = std::thread(&PoolJobManager::watchdogLoop, this);
}

MiningJob PoolJobManager::getJob()
{
    auto c = snapshotClient();
    if(!c) return MiningJob{};
    PoolJob pj = c->getJob();
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
    auto c = snapshotClient();
    if(c && c->submit(job_id, nonce, bytesToHex(hash)))
        submitted_.fetch_add(1, std::memory_order_relaxed);
    else
        sendFailed_.fetch_add(1, std::memory_order_relaxed);
}

uint64_t PoolJobManager::getSubmittedCount() const
{
    return submitted_.load(std::memory_order_relaxed);
}

uint64_t PoolJobManager::getAcceptedCount() const
{
    // Meme lock que le pliage au swap (watchdogLoop) : base + client courant
    // lus atomiquement -> jamais l'ancien client compte deux fois.
    std::lock_guard<std::mutex> lk(clientMutex_);
    return acceptedBase_ + (client_ ? client_->getAcceptedCount() : 0);
}

uint64_t PoolJobManager::getRejectedCount() const
{
    std::lock_guard<std::mutex> lk(clientMutex_);
    return rejectedBase_ + (client_ ? client_->getRejectedCount() : 0);
}

uint64_t PoolJobManager::getSendFailedCount() const
{
    return sendFailed_.load(std::memory_order_relaxed);
}
