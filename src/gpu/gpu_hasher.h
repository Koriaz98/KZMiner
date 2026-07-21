#pragma once
#include <cstdint>
#include <cstddef>

// Interface generique pour un "hasheur" GPU : encapsule tout l'etat
// persistant necessaire pour hasher un lot (batch) de mots de passe sur
// un device GPU donne, sans jamais exposer au code appelant (GpuMiner)
// les details de l'implementation concrete (CUDA/argon2-gpu aujourd'hui
// pour Argon2id, potentiellement une toute autre approche pour un futur
// algorithme). GpuMiner ne connait que cette interface - aucun include
// CUDA necessaire de son cote.
class GpuHasher
{
public:
    virtual ~GpuHasher() = default;

    // Definit le mot de passe (l'input complet du batch courant, deja
    // construit par l'appelant - nonce injecte dedans ou fourni
    // separement selon le schema de l'algorithme concret) pour
    // l'emplacement "index" du batch.
    virtual void setPassword(size_t index, const uint8_t* data, size_t len) = 0;

    // Lance le calcul du batch entier (bloquant jusqu'a completion).
    virtual void computeBatch() = 0;

    // Recupere le resultat de hachage (32 octets) pour l'emplacement
    // "index" du batch courant, dans le buffer fourni par l'appelant.
    virtual void getHash(size_t index, uint8_t* out) const = 0;

    // Taille du batch pour laquelle ce hasheur a ete construit.
    virtual size_t batchSize() const = 0;

    // Definit le sel utilise pour le batch courant. No-op par defaut :
    // les algorithmes a sel fixe (BTC09) n'ont pas besoin de le
    // redefinir. Les algorithmes a sel variable par job (Blocknet)
    // redefinissent cette methode - GpuMiner l'appelle de facon
    // generique a chaque changement de job, sans avoir besoin de
    // savoir si l'algorithme courant en a reellement besoin.
    virtual void setSalt(const uint8_t* /*data*/, size_t /*len*/) {}
};
