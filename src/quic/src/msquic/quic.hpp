#pragma once

#ifdef WIN32
#pragma comment(lib, "Ntdll.lib")
#endif

#define QUIC_API_ENABLE_PREVIEW_FEATURES
#include <msquic.h>
#undef QUIC_API_ENABLE_PREVIEW_FEATURES

#include <functional>
#include <memory>
#include <string>
#include <deque>

#include <atomic>
#include <future>
#include <optional>

#include <span.hpp>

namespace beyond_impl
{

using Bytes = nonstd::span<uint8_t>;

class MsQuicServer;
class MsQuicConfiguration;
class MsQuicClient;
class MsQuicConnection;
class IMsQuicConnectionHandler;
class MsQuicStream;
class MsQuicDatagram;

using MsQuicConnectionPtr = std::unique_ptr<MsQuicConnection>;
using MsQuicStreamPtr = std::unique_ptr<MsQuicStream>;

class MsQuicContext
{
public:
    const QUIC_API_TABLE* Api = nullptr;
    HQUIC hRegistration = nullptr;

    /*~MsQuicContext();
    MsQuicContext operator=(MsQuicContext&) = delete;
    MsQuicContext (const MsQuicContext&) = delete;*/

    static void Open(MsQuicContext& Ctx, const std::string& AppName="");
    static void Close(MsQuicContext&);

    bool IsValid() { return Api != nullptr; }
};

/** Configuration. Credentials can be configured with SetCertificate*() methods. */
struct MsQuicConfiguration
{
    MsQuicConfiguration();
    void Apply(MsQuicContext* MsQuic, HQUIC& hConfiguration);

    static const uint16_t DefaultPort;
    static const std::string AlpnName;
    static const uint16_t DefaultStreamCount;

    /** Indicate to the TLS layer that NO server certificate validation is to
     *  be performed. THIS IS DANGEROUS; DO NOT USE IN PRODUCTION */
    void DisableCertificateValidation();

    /** Is this configuration for a client or a server */
    void SetClient(bool);

    /** Require clients to provide authentication for the handshake to succeed. Not supported on client. */
    void RequireClientAuthentication();

    void SetCertificateFiles(const std::string& PrivateKeyPathIn, const std::string& CertificatePathIn);

    void SetCertificatePKCS12(nonstd::span<unsigned char> Blob);

    void SetKeepAlive(uint32_t IntervalMs);

    QUIC_SETTINGS Settings;

private:
    union {
        QUIC_CERTIFICATE_HASH CertificateHash;
        QUIC_CERTIFICATE_HASH_STORE CertificateHashStore;
        QUIC_CERTIFICATE_FILE CertificateFile;
        QUIC_CERTIFICATE_FILE_PROTECTED CertificateFileProtected;
        QUIC_CERTIFICATE_PKCS12 CertificatePkcs12;
    };

    QUIC_CREDENTIAL_CONFIG CredentialConfig;
    std::vector<char> CredentialBuffer;
};

/** Client */
class MsQuicClient
{
public:
    MsQuicClient(MsQuicContext*);
    ~MsQuicClient();

    void Configure(MsQuicConfiguration);

    MsQuicConnectionPtr Connect(IMsQuicConnectionHandler* Client, const std::string& Host, uint16_t Port);

private:
    MsQuicClient(const MsQuicClient&) = delete;
    MsQuicClient& operator=(const MsQuicClient&) = delete;

    MsQuicContext* MsQuic;
    HQUIC hConfiguration;
};

class IMsQuicServerConnectionHandler
{
public:
    virtual void OnConnection(MsQuicServer* Listener, MsQuicConnectionPtr Connection, const QUIC_NEW_CONNECTION_INFO& Info) {}
    virtual ~IMsQuicServerConnectionHandler() {}
};

/** Server */
class MsQuicServer
{
public:
    MsQuicServer(MsQuicContext*);
    virtual ~MsQuicServer();

    void Configure(MsQuicConfiguration);

    void Start(const std::string& Address);
    void Stop();

    uint16_t GetPort();

    void SetCallbackHandler(IMsQuicServerConnectionHandler* Handler) { CallbackHandler = Handler; }

protected:
    /** Called onnce for connections. Implementation must call Connection->SetObserver() */
    virtual void OnConnection(MsQuicConnectionPtr Connection, const QUIC_NEW_CONNECTION_INFO& Info);

private:
    MsQuicServer(const MsQuicServer&) = delete;
    MsQuicServer& operator=(const MsQuicServer&) = delete;

    MsQuicContext* MsQuic;
    IMsQuicServerConnectionHandler* CallbackHandler;

    HQUIC hConfiguration;
    HQUIC hListener;
};

/** Interface for Open()/Close() */
class MsQuicOpenable
{
public:
    std::future<QUIC_STATUS> Open();
    std::future<QUIC_STATUS> Close();
    
    bool IsOpen() const;

protected:
    MsQuicOpenable();

    // Set value but do not set the promise (useful for example if state can be considered open after initialization)
    void SetOpenValue(bool);

    void SetOpenStatus(QUIC_STATUS);
    void SetCloseStatus(QUIC_STATUS);

private:
    std::promise<QUIC_STATUS> PromiseOpen;
    std::promise<QUIC_STATUS> PromiseClose;
    std::atomic_bool bOpen;
    std::atomic_bool bClosed;
};

} // beyond_impl
