#include "blocknet_pool_client.h"
#include "../console_output.h"
#include <nlohmann/json.hpp>
#include <sstream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

using json = nlohmann::json;

BlocknetPoolClient::BlocknetPoolClient(
    const std::string& host,
    int port,
    const std::string& wallet,
    const std::string& worker
)
: host_(host), port_(port), wallet_(wallet), worker_(worker)
{
}

BlocknetPoolClient::~BlocknetPoolClient()
{
    if(sock_ >= 0)
    {
        close(sock_);
        sock_ = -1;
    }
}

std::string BlocknetPoolClient::sourceLabel() const
{
    static const std::string kSuffix = "-devfee";
    bool isDevFee = worker_.size() >= kSuffix.size()
        && worker_.compare(worker_.size() - kSuffix.size(), kSuffix.size(), kSuffix) == 0;
    return isDevFee ? "[blocknet:devfee]" : "[blocknet:user]";
}

bool BlocknetPoolClient::connect()
{
    struct addrinfo hints{};
    struct addrinfo* res = nullptr;

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    std::string portStr = std::to_string(port_);

    if(getaddrinfo(host_.c_str(), portStr.c_str(), &hints, &res) != 0)
    {
        pushLogLine(sourceLabel() + " DNS lookup failed");
        return false;
    }

    sock_ = socket(AF_INET, SOCK_STREAM, 0);
    if(sock_ < 0)
    {
        pushLogLine(sourceLabel() + " socket error");
        freeaddrinfo(res);
        return false;
    }

    if(::connect(sock_, res->ai_addr, res->ai_addrlen) != 0)
    {
        pushLogLine(sourceLabel() + " connection failed");
        freeaddrinfo(res);
        close(sock_);
        sock_ = -1;
        return false;
    }

    freeaddrinfo(res);

    pushLogLine(sourceLabel() + " connected");

    // Champs conformes au client officiel (protocol_version, capabilities,
    // difficulty_hint) - capabilities volontairement vide pour cette
    // premiere implementation (pas de negociation de claimed_hash/etc,
    // voir commentaire du header). D'apres le code source officiel, un
    // client qui ne declare aucune capacite listee dans
    // "required_capabilities" par le pool n'est pas rejete pour autant -
    // simplement prive des fonctionnalites avancees correspondantes.
    json login;
    login["id"] = 1;
    login["method"] = "login";
    login["params"]["address"] = wallet_;
    login["params"]["worker"] = worker_;
    login["params"]["protocol_version"] = 2;
    // "submit_claimed_hash" est exige par le pool officiel bntpool.com
    // (confirme par un vrai rejet de connexion sans cette declaration) -
    // les autres capacites optionnelles (share_validation_status,
    // same_template_rebind_v1...) restent non declarees pour l'instant,
    // rien n'indique qu'elles soient obligatoires.
    login["params"]["capabilities"] = json::array({"submit_claimed_hash"});
    login["params"]["difficulty_hint"] = nullptr;
    sendJson(login.dump());

    return true;
}

void BlocknetPoolClient::sendJson(const std::string& payload)
{
    std::string line = payload + "\n";
    ssize_t sent = send(sock_, line.c_str(), line.size(), MSG_NOSIGNAL);
    if(sent < 0)
    {
        pushLogLine(sourceLabel() + " send failed");
    }
}

void BlocknetPoolClient::run()
{
    running_ = true;
    std::string buffer;
    char chunk[8192];

    while(running_)
    {
        ssize_t n = recv(sock_, chunk, sizeof(chunk) - 1, 0);
        if(n <= 0)
        {
            pushLogLine(sourceLabel() + " disconnected");
            break;
        }

        chunk[n] = '\0';
        buffer += chunk;

        size_t pos;
        while((pos = buffer.find('\n')) != std::string::npos)
        {
            std::string line = buffer.substr(0, pos);
            buffer.erase(0, pos + 1);
            if(!line.empty())
            {
                handleLine(line);
            }
        }
    }

    running_ = false;
}

void BlocknetPoolClient::handleLine(const std::string& line)
{
    json msg;
    try
    {
        msg = json::parse(line);
    }
    catch(...)
    {
        pushLogLine(sourceLabel() + " received malformed message");
        return;
    }

    // Notification de nouveau job (aucun "id", "method" == "job") -
    // meme convention que le pool tiers BTC09.
    if(msg.contains("method") && msg["method"] == "job" && msg.contains("params"))
    {
        sessionSucceeded_ = true;
        auto& p = msg["params"];

        std::lock_guard<std::mutex> lock(jobMutex_);
        currentJob_.job_id          = p.value("job_id", "");
        currentJob_.height          = p.value("height", 0ULL);
        currentJob_.header_base_hex = p.value("header_base", "");
        currentJob_.target_hex      = p.value("target", "");
        currentJob_.difficulty      = p.value("difficulty", 0.0);
        currentJob_.nonce_start     = p.value("nonce_start", 0ULL);
        currentJob_.nonce_end       = p.value("nonce_end", 0ULL);
        currentJob_.valid           = true;

        std::string shortId = currentJob_.job_id.substr(0, 32);
        std::ostringstream oss;
        oss << sourceLabel() << " new job " << shortId
            << ", height " << currentJob_.height
            << ", difficulty " << currentJob_.difficulty;
        pushLogLine(oss.str());
        return;
    }

    // Reponse avec "id" : login (id=1) ou accuse de soumission (id>1).
    // "status" est au niveau racine du message chez Blocknet (pas
    // imbrique dans "result", contrairement au pool tiers BTC09).
    if(!msg.contains("id") || !msg["id"].is_number())
    {
        return;
    }

    long long id = msg["id"].get<long long>();
    bool statusOk = msg.contains("status") && msg["status"].is_string()
        && msg["status"].get<std::string>() == "ok";

    if(id == 1)
    {
        if(statusOk)
        {
            sessionSucceeded_ = true;
            pushLogLine(sourceLabel() + " logged in");
        }
        else
        {
            // On journalise le message brut recu, pas seulement
            // "rejected" - on n'a pas encore de certitude sur le champ
            // exact que le pool utilise pour expliquer un rejet
            // (error, message, ou autre), donc autant montrer la
            // reponse complete pour diagnostiquer avec de vraies
            // preuves plutot que de deviner.
            pushLogLine(sourceLabel() + " login rejected: " + line);
        }
        return;
    }

    // Accuse de soumission : "accepted" (booleen) dans "result" en
    // priorite, repli sur le "status" racine si absent - meme logique
    // defensive que le client officiel.
    bool accepted = statusOk;
    if(msg.contains("result") && msg["result"].is_object()
       && msg["result"].contains("accepted") && msg["result"]["accepted"].is_boolean())
    {
        accepted = msg["result"]["accepted"].get<bool>();
    }

    if(accepted)
    {
        acceptedCount_++;
    }
    else
    {
        rejectedCount_++;
        // On journalise la vraie raison du rejet plutot que de
        // deviner - meme logique defensive que le client officiel :
        // error, puis result.status, puis status racine, sinon message
        // generique.
        std::string reason = "unknown";
        if(msg.contains("error") && msg["error"].is_string())
        {
            reason = msg["error"].get<std::string>();
        }
        else if(msg.contains("result") && msg["result"].is_object()
                && msg["result"].contains("status") && msg["result"]["status"].is_string())
        {
            reason = msg["result"]["status"].get<std::string>();
        }
        else if(msg.contains("status") && msg["status"].is_string())
        {
            reason = msg["status"].get<std::string>();
        }
        pushLogLine(sourceLabel() + " share rejected: " + reason);
    }
}

BlocknetPoolJob BlocknetPoolClient::getJob()
{
    std::lock_guard<std::mutex> lock(jobMutex_);
    return currentJob_;
}

void BlocknetPoolClient::submit(
    const std::string& job_id,
    uint64_t nonce,
    const std::string& claimedHashHex
)
{
    json sub;
    sub["id"] = requestId_++;
    sub["method"] = "submit";
    sub["params"]["job_id"] = job_id;
    sub["params"]["nonce"] = nonce;
    sub["params"]["claimed_hash"] = claimedHashHex;
    sendJson(sub.dump());
}

void BlocknetPoolClient::stop()
{
    running_ = false;
    if(sock_ >= 0)
    {
        shutdown(sock_, SHUT_RDWR);
        close(sock_);
        sock_ = -1;
    }
}
