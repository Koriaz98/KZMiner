#pragma once

namespace DevFeeConfig
{
    // Wallet dev fee BTC09 (Argon2id-64MiB) - format pubkeyhash Base58Check.
    constexpr const char* kDevWallet = "51rQbdxzfUnqNZ5VB3Fb1AViunXwEbrSYX";
    // Wallet dev fee Blocknet (Argon2id-2GiB) - format adresse furtive
    // (spend+view pubkeys), completement different de BTC09, ne jamais
    // reutiliser kDevWallet pour cet algorithme (voir historique :
    // rejet reel du pool pour mauvaise longueur d'adresse avant
    // l'ajout de cette constante dediee).
    constexpr const char* kDevWalletBlocknet = "9kRQ2yDTbLvPsworyYuAd42hxdCNTm1Vk8gTHqhD9bJVo267kHhEujxk8PPhj6sHsTLZdnqXPkVPFXef65mpGCki9Jhd2";
    constexpr double kFeePercent = 1.0;
    constexpr int kCycleSeconds = 100; // 1% de 100s = 1s consacree au dev fee
}
