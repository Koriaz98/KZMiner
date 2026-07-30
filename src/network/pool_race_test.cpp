// Test de course pour la reconnexion des managers de pool (issue #1).
//
// Reproduit la race entre watchdogLoop() (qui remplace client_) et les
// threads workers qui dereferencent client_ via getJob()/submitNonce()/
// getAcceptedCount()/getRejectedCount(), sans synchronisation.
//
// Se compile avec -fsanitize=thread (ThreadSanitizer) et sans CUDA :
// tout le bug vit dans le code CPU du manager reseau.
//
// Protocole : on pointe le manager sur 127.0.0.1:<port ferme> (connect
// refuse instantanement -> le watchdog boucle et reassigne client_ en
// permanence), on force KZMINER_RECONNECT_DELAY_SECONDS=0 pour maximiser
// la cadence des swaps, puis plusieurs threads lecteurs martelent les
// methodes qui dereferencent client_. TSan doit signaler la data race
// (lecture/ecriture concurrente de client_) AVANT le correctif, et ne
// plus rien signaler APRES (shared_ptr + snapshotClient() + mutex).
//
// Cible CMake : pool-race-test (voir CMakeLists.txt).

#include "mining_source.h"
#include "pool_job_manager.h"
#include "blocknet_pool_job_manager.h"

#include <atomic>
#include <thread>
#include <vector>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <cstdint>

namespace
{
    // Duree de la fenetre de stress (assez pour que TSan attrape la
    // course : avec un delai de reconnexion de 0s, les swaps sont
    // constants et les lecteurs martelent en continu).
    constexpr int kStressSeconds = 3;
    constexpr int kReaderThreads = 6;

    void stressManager(const char* label, MiningSource& mgr)
    {
        std::printf("[pool-race-test] stressing %s ...\n", label);
        mgr.start();

        std::atomic<bool> stop{false};
        std::vector<std::thread> readers;
        const std::vector<uint8_t> dummyHash(32, 0xAB);

        for(int i = 0; i < kReaderThreads; i++)
        {
            readers.emplace_back([&]()
            {
                while(!stop.load(std::memory_order_relaxed))
                {
                    // Chacune de ces 4 methodes dereference client_ sans
                    // verrou dans le code d'origine, en concurrence avec
                    // le swap du watchdog.
                    MiningJob job = mgr.getJob();
                    mgr.submitNonce(job.job_id, 0x123456789aull, dummyHash,
                                    job.height, false);
                    volatile uint64_t a = mgr.getAcceptedCount();
                    volatile uint64_t r = mgr.getRejectedCount();
                    (void)a; (void)r;
                }
            });
        }

        std::this_thread::sleep_for(std::chrono::seconds(kStressSeconds));
        stop.store(true, std::memory_order_relaxed);
        for(auto& t : readers) t.join();
        std::printf("[pool-race-test] %s readers joined, tearing down\n", label);
        // Le destructeur du manager (a la sortie du scope appelant) fait
        // aussi tourner le teardown en concurrence du watchdog encore
        // vivant : c'est un second site de course sur client_ que le
        // test exerce naturellement.
    }
}

int main()
{
    // Force des reconnexions immediates : le watchdog reassigne client_
    // en boucle serree, maximisant la fenetre de course.
    setenv("KZMINER_RECONNECT_DELAY_SECONDS", "0", 1);

    // Port 1 sur loopback : rien n'ecoute -> connect() est refuse
    // instantanement (ECONNREFUSED), pas d'attente reseau.
    {
        PoolJobManager mgr("127.0.0.1", 1, "test-wallet", "test-worker");
        stressManager("btc09-pool", mgr);
    }
    {
        BlocknetPoolJobManager mgr("127.0.0.1", 1, "test-wallet", "test-worker");
        stressManager("blocknet-pool", mgr);
    }

    std::printf("[pool-race-test] done\n");
    return 0;
}
