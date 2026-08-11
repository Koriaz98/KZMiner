#pragma once
#include <string>
#include <cstdint>
#include <list>
#include <unordered_map>

// Cache borne (LRU) associant job_id -> prochain nonce a essayer.
//
// UTILISE PAR WORKER (variable locale au thread) : aucun etat partage
// entre workers, donc AUCUNE synchronisation necessaire.
//
// But : quand un worker RETROUVE un job qu'il minait deja (typiquement au
// retour d'une excursion dev fee : user -> dev -> user, ou un job
// transitoire), il doit REPRENDRE le nonce la ou il s'etait arrete, au
// lieu de repartir du debut de la fenetre. Repartir du debut re-hache les
// memes nonces et re-soumet les memes shares -> le pool les rejette en
// "duplicate share" (bug historique, prouve sur les soumissions reelles).
//
// Borne (LRU, defaut 64 entrees) pour ne jamais fuir en memoire : chez
// BTC09 les job_id ne se repetent jamais, la map se remplit puis evince
// les plus anciens. Les job_id "chauds" (job user + job dev courants, une
// poignee) sont rafraichis en permanence par find() et ne sont donc
// jamais evinces.
class NonceResumeCache
{
public:
    explicit NonceResumeCache(size_t maxEntries = 64) : max_(maxEntries) {}

    // Nonce de reprise pour jobId s'il est connu (et rafraichit sa
    // recence), sinon nullptr (job jamais vu).
    const uint64_t* find(const std::string& jobId)
    {
        auto it = map_.find(jobId);
        if(it == map_.end()) return nullptr;
        order_.splice(order_.end(), order_, it->second.second);
        return &it->second.first;
    }

    // Memorise/actualise le nonce de reprise de jobId (evince le moins
    // recemment utilise si la capacite est atteinte).
    void store(const std::string& jobId, uint64_t nonce)
    {
        auto it = map_.find(jobId);
        if(it != map_.end())
        {
            it->second.first = nonce;
            order_.splice(order_.end(), order_, it->second.second);
            return;
        }
        if(map_.size() >= max_ && !order_.empty())
        {
            map_.erase(order_.front());
            order_.pop_front();
        }
        auto lit = order_.insert(order_.end(), jobId);
        map_.emplace(jobId, std::make_pair(nonce, lit));
    }

private:
    size_t max_;
    // front = moins recent, back = plus recent.
    std::list<std::string> order_;
    std::unordered_map<std::string,
        std::pair<uint64_t, std::list<std::string>::iterator>> map_;
};
