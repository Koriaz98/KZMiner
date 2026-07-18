#pragma once
#include <string>
#include <vector>
#include <unistd.h>

// Point d'entree unique pour toute sortie "log" du mineur (messages
// reseau, GPU, dev fee...). En sortie fichier (nohup > fichier.log,
// ou toute sortie non interactive comme via HiveOS avec `tee`),
// pushLogLine() ecrit immediatement, comme avant - comportement
// inchange. En terminal interactif (SSH direct, screen, tmux), les
// lignes sont mises en file d'attente ; c'est le rafraichissement du
// tableau de bord (toutes les 1s) qui les affiche, regroupees avec le
// panneau en un seul bloc atomique - evite toute ecriture concurrente
// directe au terminal, source des problemes d'affichage rencontres
// avec certains multiplexeurs de terminal (screen).
bool isInteractiveTerminal();
void pushLogLine(const std::string& line);
std::vector<std::string> recentLogLines();
