#include "blocknet_pool_job_manager.h"
#include "../console_output.h"
#include <sstream>
#include <iomanip>
#include <chrono>
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

BlocknetPoolJobManager::BlocknetPoolJobManager(
    const std::string& host,
    int port,
    const std::string& wallet,
    const std::string& worker
)
: host_(host), port_(port), wallet_(wallet), worker_(worker)
, client_(std::make_shared<BlocknetPoolClient>(host, port, wallet, worker))
{
}

std::shared_ptr<BlocknetPoolClient> BlocknetPoolJobManager::snapshotClient() const
{
    std::lock_guard<std::mutex> lk(clientMutex_);
    return client_;
}

BlocknetPoolJobManager::~BlocknetPoolJobManager()
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

int BlocknetPoolJobManager::reconnectDelaySeconds() const
{
    // Delai fixe 60s en production. Surchargeable via la variable
    // d'environnement KZMINER_RECONNECT_DELAY_SECONDS UNIQUEMENT pour les
    // tests (ex: 0 pour forcer des reconnexions rapides sous
    // ThreadSanitizer). Absente en prod -> comportement inchange.
    if(const char* env = std::getenv("KZMINER_RECONNECT_DELAY_SECONDS"))
    {
        return std::atoi(env);
    }
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

        bool loginRejected = false;
        {
            // Lecture d'etat de l'ancien client via snapshot (le watchdog
            // pourrait sinon lire client_ pendant qu'il le remplace).
            auto old = snapshotClient();
            bool succeeded = old && old->hadSuccessfulSession();
            loginRejected  = old && old->loginWasRejected();
            if(succeeded)
            {
                consecutiveFailures_ = 0;
            }
            else
            {
                if(consecutiveFailures_ < 10) consecutiveFailures_++;
            }

            // Politique fatale generique : N rejets de login EXPLICITES
            // consecutifs -> arret complet (voir login_fatal_policy.h). Ne
            // s'applique qu'au rejet explicite ; une coupure reseau garde la
            // reconnexion habituelle (issue #1) intacte. worker_ suffixe par
            // "-devfee" pour la source dev (etiquette de log seulement).
            static const std::string kDevSuffix = "-devfee";
            bool isDev = worker_.size() >= kDevSuffix.size()
                && worker_.compare(worker_.size() - kDevSuffix.size(), kDevSuffix.size(), kDevSuffix) == 0;
            std::string label = std::string("[blocknet:") + (isDev ? "devfee" : "user") + "]";
            if(loginPolicy_.onSessionEnded(succeeded, loginRejected, label))
            {
                // On cesse de reconnecter ; main lira hasFatalError() et
                // declenchera l'arret propre de tout le mineur.
                running_ = false;
                break;
            }
        }

        // Backoff court dedie apres un rejet de login (l'adresse ne se
        // corrige pas toute seule), sinon le delai de reconnexion reseau.
        int delay = loginPolicy_.backoffSeconds(reconnectDelaySeconds());

        if(!loginRejected)
        {
            pushLogLine("[blocknet] connection lost, reconnecting in " + std::to_string(delay) + "s...");
        }

        std::this_thread::sleep_for(std::chrono::seconds(delay));
        if(!running_) break;

        // Client neuf construit + connecte EN LOCAL (invisible des workers) ;
        // connect() bloquant tenu HORS du lock.
        auto newClient = std::make_shared<BlocknetPoolClient>(host_, port_, wallet_, worker_);
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
            pushLogLine("[blocknet] reconnection failed, retrying in " + std::to_string(nextDelay) + "s");
            netThread_ = std::thread([](){});
        }
    }
}

void BlocknetPoolJobManager::start()
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
        pushLogLine("[blocknet] initial connection failed");
    }
    if(c) netThread_ = std::thread([c](){ c->run(); });
    watchdogThread_ = std::thread(&BlocknetPoolJobManager::watchdogLoop, this);
}

MiningJob BlocknetPoolJobManager::getJob()
{
    auto c = snapshotClient();
    if(!c) return MiningJob{};
    BlocknetPoolJob pj = c->getJob();
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
    auto c = snapshotClient();
    if(c && c->submit(job_id, nonce, bytesToHex(hash)))
        submitted_.fetch_add(1, std::memory_order_relaxed);
    else
        sendFailed_.fetch_add(1, std::memory_order_relaxed);
}

uint64_t BlocknetPoolJobManager::getSubmittedCount() const
{
    return submitted_.load(std::memory_order_relaxed);
}

uint64_t BlocknetPoolJobManager::getAcceptedCount() const
{
    // Meme lock que le pliage au swap (watchdogLoop) : base + client courant
    // lus atomiquement -> jamais l'ancien client compte deux fois.
    std::lock_guard<std::mutex> lk(clientMutex_);
    return acceptedBase_ + (client_ ? client_->getAcceptedCount() : 0);
}

uint64_t BlocknetPoolJobManager::getRejectedCount() const
{
    std::lock_guard<std::mutex> lk(clientMutex_);
    return rejectedBase_ + (client_ ? client_->getRejectedCount() : 0);
}

uint64_t BlocknetPoolJobManager::getSendFailedCount() const
{
    return sendFailed_.load(std::memory_order_relaxed);
}
