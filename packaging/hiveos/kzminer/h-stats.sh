#!/usr/bin/env bash
# Lit le fichier JSON natif ecrit par KZMiner (voir status_json.cpp
# dans le depot source) plutot que d'analyser le texte du tableau de
# bord humain - plus robuste face aux futurs changements de format
# d'affichage.
#
# A VERIFIER AU PREMIER TEST REEL : necessite `jq`, dont la presence
# par defaut sur HiveOS n'a pas ete confirmee.

status_file=/tmp/kzminer-status.json

if [ ! -s "$status_file" ]; then
    khs=0
    stats='{"hs": [], "hs_units": "hs", "temp": [], "fan": [], "uptime": 0, "ver": "kzminer", "ar": [0, 0], "algo": "unknown"}'
else
    khs=$(jq -r '.hashrate_total_hs / 1000' "$status_file")
    hs_gpu=$(jq -c '[.gpus[].hashrate_hs]' "$status_file")
    temp_gpu=$(jq -c '[.gpus[].temp_c]' "$status_file")
    fan_gpu=$(jq -c '[.gpus[].fan_percent]' "$status_file")
    uptime=$(jq -r '.uptime' "$status_file")
    ver=$(jq -r '.ver' "$status_file")
    accepted=$(jq -r '.shares.accepted' "$status_file")
    rejected=$(jq -r '.shares.rejected' "$status_file")
    algo=$(jq -r '.algo' "$status_file")

    stats=$(jq -n \
        --argjson hs "$hs_gpu" \
        --argjson temp "$temp_gpu" \
        --argjson fan "$fan_gpu" \
        --argjson uptime "$uptime" \
        --arg ver "kzminer/$ver" \
        --argjson accepted "$accepted" \
        --argjson rejected "$rejected" \
        --arg algo "$algo" \
        '{hs: $hs, hs_units: "hs", temp: $temp, fan: $fan, uptime: $uptime, ver: $ver, ar: [$accepted, $rejected], algo: $algo}')
fi
