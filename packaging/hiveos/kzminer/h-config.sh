#!/usr/bin/env bash
# KZMiner ne necessite pas de fichier de configuration JSON : tous ses
# parametres sont passes directement en ligne de commande. Ce script
# se contente de verifier que le champ "Miner config" du flight sheet
# HiveOS (expose ici via $CUSTOM_TEMPLATE) a bien ete rempli - c'est
# lui qui contient la commande KZMiner complete, avec %WAL%/%URL%/
# %WORKER_NAME% deja substitues par Hive avant d'arriver ici.
#
# A VERIFIER AU PREMIER TEST REEL : $CUSTOM_TEMPLATE est le nom de
# variable le plus probable d'apres la documentation publique
# disponible, mais n'a pas ete confirme sur un systeme HiveOS reel.

. "$MINER_DIR/$CUSTOM_MINER/h-manifest.conf"

if [ -z "$CUSTOM_TEMPLATE" ]; then
    echo "kzminer: le champ 'Miner config' du flight sheet est vide" >&2
    echo "kzminer: exemple: --mode solo -o https://btc09.org -u %WAL% --cpu --gpu --intensity 5" >&2
fi
