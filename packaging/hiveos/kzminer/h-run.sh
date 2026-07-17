#!/usr/bin/env bash
# Le framework HiveOS se place deja dans le dossier d'installation de
# ce mineur avant d'executer ce script. La vraie commande a executer
# se trouve dans $CUSTOM_USER_CONFIG (definie dans /hive-config/
# wallet.conf), avec %URL%/%WAL%/%WORKER_NAME% deja substitues par
# Hive - confirme sur un vrai systeme HiveOS le 17/07/2026.

. /hive-config/wallet.conf
. ./h-manifest.conf

log_file="$CUSTOM_LOG_BASENAME.log"
mkdir -p "$(dirname "$log_file")"

exec ./kzminer-v0.9.7 $CUSTOM_USER_CONFIG >>"$log_file" 2>&1
