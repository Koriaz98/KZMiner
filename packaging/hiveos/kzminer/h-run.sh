#!/usr/bin/env bash

. "$MINER_DIR/$CUSTOM_MINER/h-manifest.conf"

cd "$MINER_DIR/$CUSTOM_MINER" || exit 1

log_file="$CUSTOM_LOG_BASENAME.log"
mkdir -p "$(dirname "$log_file")"

# $CUSTOM_TEMPLATE contient la commande KZMiner complete, deja
# substituee par Hive (voir h-config.sh pour le detail).
./kzminer-v0.9.7 $CUSTOM_TEMPLATE >>"$log_file" 2>&1 &
echo $! > "$MINER_DIR/$CUSTOM_MINER/kzminer.pid"
