#pragma once
#include "../../algo/blocknet_argon2id/blocknet_argon2id_algorithm.h"
#include <memory>

// Fabrique l'instance BlocknetArgon2idAlgorithm - les parametres
// Argon2id de Blocknet (2 GiB, 1 iteration) sont deja fixes dans
// BlocknetArgon2idAlgorithm elle-meme (contrairement a BTC09, aucune
// variation attendue), donc cette fabrique n'a besoin de rien de plus
// qu'une simple construction par defaut.
inline std::unique_ptr<BlocknetArgon2idAlgorithm> makeBlocknetAlgorithm()
{
    return std::make_unique<BlocknetArgon2idAlgorithm>();
}
