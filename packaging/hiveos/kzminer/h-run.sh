#!/usr/bin/env bash
# $CUSTOM_USER_CONFIG contient la commande KZMiner, avec %WAL% et
# %WORKER_NAME% deja substitues par Hive - mais PAS %URL% (confirme
# sur un vrai systeme le 17/07/2026). On le remplace nous-memes avec
# $CUSTOM_URL, qui contient la vraie valeur.
#
# Le binaire est nomme "kzminer" (sans suffixe de version) a
# l'interieur de ce paquet specifiquement pour HiveOS, pour ne jamais
# avoir a modifier ce script lors des futures mises a jour de version.

. /hive-config/wallet.conf
. ./h-manifest.conf

log_file="$CUSTOM_LOG_BASENAME.log"
mkdir -p "$(dirname "$log_file")"

final_config="${CUSTOM_USER_CONFIG//%URL%/$CUSTOM_URL}"

exec ./kzminer $final_config >>"$log_file" 2>&1
