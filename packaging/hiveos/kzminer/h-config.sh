#!/usr/bin/env bash
# KZMiner ne necessite pas de fichier de configuration JSON : tous ses
# parametres sont passes directement en ligne de commande, via
# $CUSTOM_USER_CONFIG (definie dans /hive-config/wallet.conf).

. /hive-config/wallet.conf

if [ -z "$CUSTOM_USER_CONFIG" ]; then
    echo "kzminer: le champ 'Extra config arguments' du flight sheet est vide" >&2
    echo "kzminer: exemple: --mode solo -o %URL% -u %WAL%.%WORKER_NAME% --cpu --gpu --intensity 5" >&2
fi
