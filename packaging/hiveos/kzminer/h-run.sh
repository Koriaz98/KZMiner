#!/usr/bin/env bash

. "$MINER_DIR/$CUSTOM_MINER/h-manifest.conf"

cd "$MINER_DIR/$CUSTOM_MINER" || exit 1

log_file="$CUSTOM_LOG_BASENAME.log"
mkdir -p "$(dirname "$log_file")"

# Le screen "miner" est cree par le framework HiveOS lui-meme autour
# de ce script - KZMiner doit tourner au premier plan ici (pas de &),
# sinon ce script se termine immediatement et la session screen meurt
# avec lui.
exec ./kzminer-v0.9.7 $CUSTOM_TEMPLATE >>"$log_file" 2>&1
