#pragma once

// Verifie s'il existe une release GitHub plus recente que la version
// actuelle et affiche une simple notification si c'est le cas.
// Ne telecharge et ne remplace JAMAIS le binaire automatiquement.
// Echoue silencieusement en cas de probleme reseau (pas bloquant).
void checkForUpdate();
