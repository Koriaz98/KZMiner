#pragma once
#include <mutex>

// Verrou global partage par tous les points du code qui ecrivent sur
// la console (std::cout/std::cerr) depuis un thread susceptible de
// tourner en parallele du thread principal (qui affiche le tableau de
// bord). Garantit qu'un seul thread ecrit a la fois, evitant que ses
// lignes ne s'entrelacent avec le tableau au milieu de son affichage.
inline std::mutex& consoleMutex()
{
    static std::mutex m;
    return m;
}
