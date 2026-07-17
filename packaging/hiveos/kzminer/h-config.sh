#!/usr/bin/env bash
# KZMiner ne necessite pas de fichier de configuration JSON : tous ses
# parametres sont passes directement en ligne de commande. Ce script
# se contente de verifier que le champ "Extra config arguments" du
# flight sheet HiveOS (expose ici via $CUSTOM_TEMPLATE) a bien ete
# rempli.

. ./h-manifest.conf

if [ -z "$CUSTOM_TEMPLATE" ]; then
    echo "kzminer: le champ 'Extra config arguments' du flight sheet est vide" >&2
    echo "kzminer: exemple: --mode solo -o %URL% -u %WAL%.%WORKER_NAME% --cpu --gpu --intensity 5" >&2
fi
