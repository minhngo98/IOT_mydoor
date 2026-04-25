#ifndef BLYNK_SIMPLE_ESP32_SSL_BOUNDED_H
#define BLYNK_SIMPLE_ESP32_SSL_BOUNDED_H

#ifndef ESP32
#error This code is intended to run on the ESP32 platform.
#endif

static const char BLYNK_DEFAULT_ROOT_CA[] =
#include <certs/letsencrypt_pem.h>

#include <BlynkApiArduino.h>
#include <Blynk/BlynkProtocol.h>
#include <Adapters/BlynkArduinoClient.h>
#include <WiFiClientSecure.h>

template <typename Client>
class BlynkArduinoClientSecureBounded
    : public BlynkArduinoClientGen<Client>
{
public:
    explicit BlynkArduinoClientSecureBounded(Client& client)
        : BlynkArduinoClientGen<Client>(client)
        , caCert(NULL)
        , handshakeTimeoutSec(BLYNK_SSL_HANDSHAKE_TIMEOUT_SEC)
    {}

    void setRootCA(const char* fp) { caCert = fp; }
    void setHandshakeTimeoutSeconds(uint32_t seconds) { handshakeTimeoutSec = seconds; }

    bool connect() {
        this->client->setHandshakeTimeout(handshakeTimeoutSec);
        this->client->setCACert(caCert);
        if (BlynkArduinoClientGen<Client>::connect()) {
            BLYNK_LOG1(BLYNK_F("Certificate OK"));
            return true;
        }
        BLYNK_LOG1(BLYNK_F("Secure connection failed"));
        return false;
    }

private:
    const char* caCert;
    uint32_t handshakeTimeoutSec;
};

template <typename Transport>
class BlynkWifiBounded
    : public BlynkProtocol<Transport>
{
    typedef BlynkProtocol<Transport> Base;

public:
    explicit BlynkWifiBounded(Transport& transp)
        : Base(transp)
    {}

    void config(const char* auth,
                const char* domain = BLYNK_DEFAULT_DOMAIN,
                uint16_t port = BLYNK_DEFAULT_PORT_SSL,
                const char* root_ca = BLYNK_DEFAULT_ROOT_CA)
    {
        Base::begin(auth);
        this->conn.begin(domain, port);
        this->conn.setRootCA(root_ca);
    }

    void config(const char* auth,
                IPAddress ip,
                uint16_t port = BLYNK_DEFAULT_PORT_SSL,
                const char* root_ca = BLYNK_DEFAULT_ROOT_CA)
    {
        Base::begin(auth);
        this->conn.begin(ip, port);
        this->conn.setRootCA(root_ca);
    }
};

#if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_BLYNK)
static WiFiClientSecure _blynkWifiClient;
static BlynkArduinoClientSecureBounded<WiFiClientSecure> _blynkTransport(_blynkWifiClient);
BlynkWifiBounded<BlynkArduinoClientSecureBounded<WiFiClientSecure> > Blynk(_blynkTransport);
#else
extern BlynkWifiBounded<BlynkArduinoClientSecureBounded<WiFiClientSecure> > Blynk;
#endif

#include <BlynkWidgets.h>

#endif
