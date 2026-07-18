#!/usr/bin/env bash
# $CUSTOM_USER_CONFIG contient la commande KZMiner, avec %WAL% et
# %WORKER_NAME% deja substitues par Hive - mais PAS %URL% (confirme
# sur un vrai systeme le 17/07/2026). On le remplace nous-memes avec
# $CUSTOM_URL, qui contient la vraie valeur.
#
# Le binaire est nomme "kzminer" (sans suffixe de version) a
# l'interieur de ce paquet specifiquement pour HiveOS, pour ne jamais
# avoir a modifier ce script lors des futures mises a jour de version.
#
# Pas de redirection de sortie ici (contrairement aux versions
# precedentes) : KZMiner doit voir un vrai terminal (isatty()) pour
# activer son panneau d'affichage fixe, comme dans une session SSH ou
# screen normale. Rediriger vers un fichier ou un tube ferait
# basculer KZMiner en mode texte simple. Les statistiques pour
# h-stats.sh viennent du fichier JSON natif (/tmp/kzminer-status.json,
# independant de toute sortie console), et HiveOS capture deja de son
# cote la sortie de la session screen qu'il cree autour de ce script.

. /hive-config/wallet.conf
. ./h-manifest.conf

final_config="${CUSTOM_USER_CONFIG//%URL%/$CUSTOM_URL}"

exec ./kzminer $final_config
