#pragma once
#include <vector>
#include <cstdint>

class Argon2Engine
{
public:
    // Hashe un buffer (le header 88 octets avec nonce insere) en Argon2id,
    // avec les parametres annonces par le job (memoire/iterations).
    static std::vector<uint8_t> hash(
        const std::vector<uint8_t>& header,
        uint32_t t_cost,
        uint32_t m_cost_kib
    );
};
