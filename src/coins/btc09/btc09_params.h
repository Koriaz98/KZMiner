#pragma once
#include "../../algo/argon2id/argon2id_algorithm.h"
#include <memory>

// Fabrique l'instance Argon2idAlgorithm parametree pour BTC09.
// Regroupe ici tout ce qui est specifique a ce coin (salt, layout du
// header, position du nonce), pour que argon2id_algorithm.h/.cpp
// restent entierement generiques et reutilisables par un futur coin
// (Blocknet notamment, dont le password/salt sont inverses par
// rapport a BTC09 - voir la documentation de session precedente).
inline std::unique_ptr<Argon2idAlgorithm> makeBtc09Algorithm()
{
    const std::vector<uint8_t> kSalt = {
        'B','T','C','0','9','/','p','o','w','/','v','1'
    };

    constexpr size_t kHeaderSizeBytes = 88;
    constexpr size_t kNonceOffsetBytes = 80; // derniers 8 octets du header
    constexpr uint32_t kTCost = 1;
    constexpr uint32_t kMCostKib = 65536; // 64 MiB

    return std::make_unique<Argon2idAlgorithm>(
        "Argon2id-64MiB",
        kSalt,
        kHeaderSizeBytes,
        kNonceOffsetBytes,
        kTCost,
        kMCostKib
    );
}
