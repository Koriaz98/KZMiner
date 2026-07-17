#!/usr/bin/env bash
# Le framework HiveOS se place deja dans le dossier d'installation de
# ce mineur avant d'executer ce script (confirme par le code source
# officiel : hive/miners/custom/h-run.sh fait "cd $MINER_DIR/$CUSTOM_MINER"
# avant d'appeler ce script). On utilise donc des chemins relatifs
# plutot que de reconstruire le chemin via ces variables, qui se sont
# averees vides lors d'un test reel.

. ./h-manifest.conf

log_file="$CUSTOM_LOG_BASENAME.log"
mkdir -p "$(dirname "$log_file")"

exec ./kzminer-v0.9.7 $CUSTOM_TEMPLATE >>"$log_file" 2>&1
