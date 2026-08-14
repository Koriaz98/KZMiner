#pragma once
#include <atomic>
#include <string>
#include "../console_output.h"

// Politique GENERIQUE de detection d'un login durablement rejete, partagee
// telle quelle par tous les MiningSource a login (BlocknetPoolJobManager et
// PoolJobManager BTC09 pool tiers). But : ne JAMAIS continuer a miner en
// silence sur la seule source dev quand l'adresse utilisateur est invalide.
// Sans garde-fou, le pool rejette le login user, la source dev repond, et
// 100 % du travail part au wallet dev sans la moindre erreur visible.
//
// Les valeurs de politique (nombre d'essais, backoff) vivent ICI, en un seul
// endroit : chaque manager n'y branche que sa primitive de detection
// specifique au protocole (client->loginWasRejected()/hadSuccessfulSession()).
// Seule la DETECTION d'un rejet est protocol-specifique (parsing JSON du
// login) ; la POLITIQUE, elle, ne l'est pas et n'est donc pas dupliquee.
//
// Un rejet de login est un echec DETERMINISTE (adresse invalide) : inutile
// d'attendre le long delai de reconnexion reseau, d'ou un backoff court
// dedie. Une simple coupure reseau (recv<=0 sans reponse de rejet) n'entre
// PAS dans cette politique : elle garde le comportement de reconnexion
// existant (voir issue #1), inchange.
class LoginFatalPolicy
{
public:
    static constexpr int kMaxAttempts = 3;     // rejets consecutifs -> fatal
    static constexpr int kBackoffSeconds = 5;  // backoff dedie entre 2 essais

    // Appele par le watchdog du manager apres la fin de CHAQUE session client
    // (netThread_ joint). Renvoie true si l'etat est desormais fatal.
    //   succeeded     : le pool a confirme le login ou envoye au moins un job.
    //   loginRejected : reponse protocolaire EXPLICITE de rejet du login.
    //   label         : etiquette de source pour les logs ("[blocknet:user]").
    bool onSessionEnded(bool succeeded, bool loginRejected, const std::string& label)
    {
        if(succeeded)
        {
            // Le login a fini par passer : on efface toute accumulation
            // (tolere un hoquet serveur transitoire avant un login valide).
            attempts_ = 0;
            lastWasReject_ = false;
            return false;
        }
        if(loginRejected)
        {
            lastWasReject_ = true;
            ++attempts_;
            if(attempts_ >= kMaxAttempts)
            {
                // Motif brut, neutre : cette policy peut tripper aussi bien
                // sur la source user que sur la source dev. Dans les DEUX cas
                // on cesse de reconnecter (pas de spam de logins rejetes).
                // La CONSEQUENCE "arret du minage" ne concerne que le fatal
                // user : c'est l'orchestration (main + DevFeeSource) qui la
                // decide et la journalise, pas cette policy generique.
                reason_ = label + " login rejete " + std::to_string(kMaxAttempts)
                        + " fois de suite (adresse invalide ?)";
                fatal_.store(true);   // publie reason_ (ecrit juste avant) aux lecteurs
                pushLogLine(reason_ + ", abandon des reconnexions");
                return true;
            }
            pushLogLine(label + " login rejete (tentative "
                        + std::to_string(attempts_) + "/" + std::to_string(kMaxAttempts)
                        + "), nouvel essai dans " + std::to_string(kBackoffSeconds) + "s");
            return false;
        }
        // Coupure reseau sans rejet explicite : hors politique fatale.
        lastWasReject_ = false;
        return false;
    }

    bool isFatal() const { return fatal_.load(); }
    std::string reason() const { return reason_; }

    // Delai a respecter avant la prochaine reconnexion : court et dedie si la
    // derniere session s'est soldee par un rejet de login, sinon le delai
    // reseau habituel fourni par le manager (inchange).
    int backoffSeconds(int networkDefault) const
    {
        return lastWasReject_ ? kBackoffSeconds : networkDefault;
    }

private:
    // attempts_/lastWasReject_/reason_ : ecrits UNIQUEMENT par le thread
    // watchdog (un seul par manager) -> aucune synchro necessaire entre eux.
    int attempts_ = 0;
    bool lastWasReject_ = false;
    std::string reason_;
    // fatal_ : lu par le thread principal (main) via hasFatalError(). Le
    // store(true) intervient APRES l'ecriture de reason_ -> quand main
    // observe fatal_==true, reason_ est deja visible.
    std::atomic<bool> fatal_{false};
};
