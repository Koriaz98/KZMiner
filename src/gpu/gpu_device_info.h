#pragma once
#include <string>
#include <vector>

// Identite PCI d'un device CUDA, telle que CUDA lui-meme la rapporte.
struct CudaPciIdentity
{
    // Partie "bus" de l'adresse PCI convertie en decimal - meme
    // convention que GpuStats::pciBusDecimal cote NVML/nvidia-smi, donc
    // directement comparable : c'est la cle de jointure entre les deux
    // numerotations. -1 si l'adresse PCI n'a pas pu etre lue.
    int busDecimal = -1;
    // Adresse PCI complete telle que renvoyee par CUDA, en hexadecimal
    // ("domain:bus:device.function", ex "0000:0a:00.0"). Destinee aux
    // logs, pour une confrontation directe avec nvidia-smi. Attention :
    // nvidia-smi zero-pade le domaine sur 8 chiffres
    // ("00000000:0a:00.0") la ou CUDA n'en met que 4 - seul le segment
    // bus se compare caractere pour caractere. Vide si la lecture a
    // echoue.
    std::string pciBusId;
};

// Identite PCI des devices CUDA visibles, indexee par index CUDA
// (0..count-1, la numerotation de cudaGetDeviceCount et donc la cle de
// GpuMiner::getDeviceHashes()). Le bus PCI est la seule cle de jointure
// fiable entre les deux numerotations en presence : CUDA renumerote sans
// trou les seules cartes autorisees par CUDA_VISIBLE_DEVICES, nvidia-smi
// liste toutes les cartes avec leurs numeros physiques.
// Header volontairement sans aucun en-tete CUDA (voir le .cu associe).
std::vector<CudaPciIdentity> cudaDeviceIdentities();
