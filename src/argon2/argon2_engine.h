#pragma once
#include <vector>
#include <cstdint>

class Argon2Engine
{
public:
    // Hashe un buffer (le header/mot de passe, deja construit par
    // l'algorithme appelant - voir Argon2idAlgorithm) en Argon2id, avec
    // le sel et les parametres memoire/iterations fournis par l'appelant.
    // Aucune valeur specifique a un coin n'est codee en dur ici : cette
    // classe est un simple binding vers libargon2, reutilisable tel
    // quel pour n'importe quel coin base sur Argon2id (BTC09, et plus
    // tard d'autres, chacun avec son propre sel/parametres).
    static std::vector<uint8_t> hash(
        const std::vector<uint8_t>& password,
        const std::vector<uint8_t>& salt,
        uint32_t t_cost,
        uint32_t m_cost_kib
    );
};
