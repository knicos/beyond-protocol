#include "quic.hpp"
#include "connection.hpp"

#include <filesystem>

#include <loguru.hpp>

#include "msquichelper.hpp"

using namespace beyond_impl;

////////////////////////////////////////////////////////////////////////////////

const uint16_t MsQuicConfiguration::DefaultPort = 9001;
const std::string MsQuicConfiguration::AlpnName = "beyond2";
const uint16_t MsQuicConfiguration::DefaultStreamCount = 64;

////////////////////////////////////////////////////////////////////////////////

QUIC_TLS_PROVIDER GetTlsProvider(MsQuicContext* MsQuic)
{
    QUIC_TLS_PROVIDER TlsProvider;
    uint32_t BufSize = sizeof(TlsProvider);
    CHECK_QUIC(MsQuic->Api->GetParam(nullptr, QUIC_PARAM_GLOBAL_TLS_PROVIDER, &BufSize, &TlsProvider));
    return TlsProvider;
}

////////////////////////////////////////////////////////////////////////////////

void MsQuicContext::Open(MsQuicContext& MsQuic, const std::string& AppName) 
{
    if (QUIC_FAILED(MsQuicOpen2(&MsQuic.Api)))
    {
        LOG(ERROR) << "[QUIC] MsQuicOpen2() failed";
    }

    QUIC_REGISTRATION_CONFIG Config {};
    Config.AppName = AppName.c_str();

    Config.ExecutionProfile = QUIC_EXECUTION_PROFILE_TYPE_MAX_THROUGHPUT;
    DLOG(INFO) << "[QUIC] Execution Profile: QUIC_EXECUTION_PROFILE_TYPE_MAX_THROUGHPUT";

    LOG_IF(WARNING, GetTlsProvider(&MsQuic) != QUIC_TLS_PROVIDER_OPENSSL) << "[QUIC] MsQuic not built with OpenSSL";
    DLOG(INFO) << "MsQuic opened";

    if (QUIC_FAILED(MsQuic.Api->RegistrationOpen(&Config, &MsQuic.hRegistration)))
    {
        LOG(ERROR) << "[QUIC] RegistrationOpen() failed";
        Close(MsQuic);
    }
}

void MsQuicContext::Close(MsQuicContext& Context)
{
    if (Context.Api)
    {
        if (Context.hRegistration)
        {
            Context.Api->RegistrationClose(Context.hRegistration);
            Context.hRegistration = nullptr;
        }
        
        MsQuicClose(Context.Api);
        Context.Api = nullptr;
        DLOG(INFO) << "MsQuic closed";
    }
} 

////////////////////////////////////////////////////////////////////////////////


/** Indicate to the TLS layer that NO server certificate validation is to be performed.
 *  THIS IS DANGEROUS; DO NOT USE IN PRODUCTION */
void MsQuicConfiguration::DisableCertificateValidation()
{
    CredentialConfig.Flags |= QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION;
}

/** Require clients to provide authentication for the handshake to succeed. Not supported
 *  on client. */
void MsQuicConfiguration::RequireClientAuthentication()
{
    CredentialConfig.Flags |= QUIC_CREDENTIAL_FLAG_REQUIRE_CLIENT_AUTHENTICATION;
}

void MsQuicConfiguration::SetCertificateFiles(const std::string& PrivateKeyPathIn, const std::string& CertificatePathIn)
{
    CredentialConfig.Type = QUIC_CREDENTIAL_TYPE_CERTIFICATE_FILE;

    CredentialBuffer.resize(PrivateKeyPathIn.size() + CertificatePathIn.size() + 2);
    char* PKey = CredentialBuffer.data();
    memcpy(PKey, PrivateKeyPathIn.c_str(), PrivateKeyPathIn.size() + 1);
    char* Cert = PKey + PrivateKeyPathIn.size() + 1;
    memcpy(Cert, CertificatePathIn.c_str(), CertificatePathIn.size() + 1);

    CertificateFile.PrivateKeyFile = PKey;
    CertificateFile.CertificateFile = Cert;
    CredentialConfig.CertificateFile = &CertificateFile;
}

void MsQuicConfiguration::SetCertificatePKCS12(nonstd::span<unsigned char> Blob)
{
    CredentialConfig.Type = QUIC_CREDENTIAL_TYPE_CERTIFICATE_PKCS12;
    CredentialBuffer.resize(Blob.size());
    memcpy(CredentialBuffer.data(), Blob.data(), Blob.size());
    CertificatePkcs12.Asn1Blob = (unsigned char*) CredentialBuffer.data();
    CertificatePkcs12.Asn1BlobLength = CredentialBuffer.size();
    CertificatePkcs12.PrivateKeyPassword = nullptr;
    CredentialConfig.CertificatePkcs12 = &CertificatePkcs12;
}

void MsQuicConfiguration::SetKeepAlive(uint32_t IntervalMs)
{
    Settings.KeepAliveIntervalMs = IntervalMs;
    Settings.IsSet.KeepAliveIntervalMs = true;
}

void MsQuicConfiguration::SetClient(bool IsClient)
{
    if (IsClient) { CredentialConfig.Flags |=  QUIC_CREDENTIAL_FLAG_CLIENT; }
    else          { CredentialConfig.Flags &= ~QUIC_CREDENTIAL_FLAG_CLIENT; }
}

MsQuicConfiguration::MsQuicConfiguration()
{
    memset(&Settings, 0, sizeof(Settings));
    memset(&CredentialConfig, 0, sizeof(CredentialConfig));
    CredentialConfig.Type = QUIC_CREDENTIAL_TYPE::QUIC_CREDENTIAL_TYPE_NONE;
}

/** Calls MsQuic API to create configuration handle (same steps for both client
 *  and server, flags shoud be adjusted accordingly before call).
 */
void MsQuicConfiguration::Apply(MsQuicContext* MsQuic, HQUIC& hConfiguration)
{
    LOG_IF(ERROR, (hConfiguration != nullptr)) << "[QUIC] Already configured";

    QUIC_BUFFER AlpnBuffer {
        (uint32_t) MsQuicConfiguration::AlpnName.size(),
        (uint8_t*) MsQuicConfiguration::AlpnName.c_str(),
    };

    if (Settings.IsSet.SendBufferingEnabled)
    {
        // works, but probably not a good idea
        LOG(WARNING) << "[QUIC] SendBufferingEnabled manually set";
    }
    else
    {
        Settings.IsSet.SendBufferingEnabled = 1;
        Settings.SendBufferingEnabled = 0;
    }

    Settings.IsSet.DatagramReceiveEnabled = 1;
    Settings.DatagramReceiveEnabled = 1;

    Settings.IsSet.CongestionControlAlgorithm = 1;
    
    // Requires QUIC_API_ENABLE_PREVIEW_FEATURES and lastest version of msquic
    // In simulations, with BBR single streams can perform an order of magnitude better on
    //high latency with packet loss situation compared to cubic.
    Settings.CongestionControlAlgorithm = QUIC_CONGESTION_CONTROL_ALGORITHM_BBR;

    if (!Settings.IsSet.PeerBidiStreamCount)
    {
        Settings.IsSet.PeerBidiStreamCount = 1;
        Settings.PeerBidiStreamCount = MsQuicConfiguration::DefaultStreamCount;
    }

    CHECK_QUIC(MsQuic->Api->ConfigurationOpen(
        MsQuic->hRegistration,
        &AlpnBuffer, 1,
        &Settings,
        sizeof(Settings),
        nullptr,
        &hConfiguration
    ));

    CredentialConfig.Flags |= QUIC_CREDENTIAL_FLAG_INDICATE_CERTIFICATE_RECEIVED;

    if (CredentialConfig.Flags & QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION)
    {
        LOG(WARNING) << "[QUIC] DANGEROUS: Certificate validation disabled";
    }

    // Windows: OpenSSL certificate validation can be used if QUIC_CREDENTIAL_FLAG_USE_TLS_BUILTIN_CERTIFICATE_VALIDATION
    //          is set (and MsQuic is built with OpenSSL instead of SChannel).
    if (!(CredentialConfig.Flags & QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION))
    {
        CredentialConfig.Flags |= QUIC_CREDENTIAL_FLAG_USE_TLS_BUILTIN_CERTIFICATE_VALIDATION;
    }
    CredentialConfig.Flags &= ~QUIC_CREDENTIAL_FLAG_LOAD_ASYNCHRONOUS;

    if (GetTlsProvider(MsQuic) == QUIC_TLS_PROVIDER_OPENSSL)
    {
        CredentialConfig.Flags |= QUIC_CREDENTIAL_FLAG_INDICATE_CERTIFICATE_RECEIVED
                               |  QUIC_CREDENTIAL_FLAG_USE_PORTABLE_CERTIFICATES;
    }
    
    // TODO: Actual error handling required; at minimum print human readable
    //       message (what exactly went wrong in credential configuration).
    CHECK_QUIC(MsQuic->Api->ConfigurationLoadCredential(hConfiguration, &CredentialConfig));
}

////////////////////////////////////////////////////////////////////////////////

MsQuicClient::MsQuicClient(MsQuicContext* Context) :
        MsQuic(Context), hConfiguration(nullptr)
{
    CHECK(MsQuic);
}

MsQuicClient::~MsQuicClient()
{
    if (hConfiguration)
    {
        MsQuic->Api->ConfigurationClose(hConfiguration);
        hConfiguration = nullptr;
    }
}

void MsQuicClient::Configure(MsQuicConfiguration Config)
{
    Config.SetClient(true);
    Config.Apply(MsQuic, hConfiguration);
}

MsQuicConnectionPtr MsQuicClient::Connect(IMsQuicConnectionHandler* Client, const std::string& Host, uint16_t Port)
{
    CHECK(hConfiguration) << "[QUIC] Configure() must be called before calls to Connect()";

    return MsQuicConnection::Connect(Client, MsQuic, hConfiguration, Host, Port);
}

////////////////////////////////////////////////////////////////////////////////

void MsQuicServer::OnConnection(MsQuicConnectionPtr Connection)
{
    if (CallbackHandler) { CallbackHandler->OnConnection(this, std::move(Connection)); }
}

MsQuicServer::MsQuicServer(MsQuicContext* Context) :
        MsQuic(Context), CallbackHandler(nullptr), hConfiguration(nullptr)
{
    CHECK(MsQuic);

    auto EventCb = [](HQUIC Listener, void* Context, QUIC_LISTENER_EVENT* Event)
    {
        MsQuicServer* Server = static_cast<MsQuicServer*>(Context);
        switch (Event->Type)
        {
            case QUIC_LISTENER_EVENT_NEW_CONNECTION:
            {
                // Connection completion can not be awaited here
                Server->OnConnection(
                    MsQuicConnection::Accept(
                        Server->MsQuic,
                        Server->hConfiguration,
                        Event->NEW_CONNECTION.Connection
                ));
            }
            case QUIC_LISTENER_EVENT_STOP_COMPLETE:
            {
                // ListenerClose() called in destructor.
            }
        }
        
        return QUIC_STATUS_SUCCESS;
    };

    CHECK_QUIC(MsQuic->Api->ListenerOpen(MsQuic->hRegistration, EventCb, this, &hListener));
}

MsQuicServer::~MsQuicServer()
{
    if (hListener)
    {
        MsQuic->Api->ListenerClose(hListener);
        hListener = nullptr;
    }
    
    if (hConfiguration)
    {
        MsQuic->Api->ConfigurationClose(hConfiguration);
        hConfiguration = nullptr;
    }
}

void MsQuicServer::Configure(MsQuicConfiguration Config)
{
    Config.SetClient(false);
    Config.Apply(MsQuic, hConfiguration);
}

uint16_t MsQuicServer::GetPort()
{
    if (!hListener)
    {
        return 0;
    }
    QUIC_ADDR Addr;
    uint32_t BufferLength = sizeof(Addr);

    CHECK_QUIC(MsQuic->Api->GetParam(
        hListener,
        QUIC_PARAM_LISTENER_LOCAL_ADDRESS,
        &BufferLength, (void*)&Addr));
    
    return ntohs(Addr.Ipv4.sin_port);
}

void MsQuicServer::Start(const std::string& Address)
{
    CHECK(MsQuic);

    QUIC_ADDR* QuicAddrPtr = nullptr;

    QUIC_ADDR QuicAddr {}; 

    if (!Address.empty())
    {
        if (QuicAddrFromString(
                Address.c_str(),
                MsQuicConfiguration::DefaultPort,
                &QuicAddr))
        {
            QuicAddrPtr = &QuicAddr;
        }
    }

    QUIC_BUFFER AlpnBuffer {
        (uint32_t) MsQuicConfiguration::AlpnName.size(),
        (uint8_t*) MsQuicConfiguration::AlpnName.c_str(),
    };

    CHECK_QUIC(MsQuic->Api->ListenerStart(hListener, &AlpnBuffer, 1, QuicAddrPtr));

    LOG(INFO) << "[QUIC] Listening on port " << QuicAddrGetPort(&QuicAddr) << " (UDP)";
}

void MsQuicServer::Stop()
{
    CHECK(MsQuic);
    CHECK(hListener);
    MsQuic->Api->ListenerStop(hListener);
}

////////////////////////////////////////////////////////////////////////////////

MsQuicOpenable::MsQuicOpenable() : bOpen(false) {}

std::future<QUIC_STATUS> MsQuicOpenable::Open()
{
    return PromiseOpen.get_future();
}

std::future<QUIC_STATUS> MsQuicOpenable::Close()
{
    return PromiseClose.get_future();
}

bool MsQuicOpenable::IsOpen() const
{
    return bOpen;
}

void MsQuicOpenable::SetOpenValue(bool value) { bOpen = value; }

void MsQuicOpenable::SetOpenStatus(QUIC_STATUS Result)
{
    bOpen = true;
    PromiseOpen.set_value(Result);
    PromiseClose = std::promise<QUIC_STATUS>();
}

void MsQuicOpenable::SetCloseStatus(QUIC_STATUS Result)
{
    bOpen = false;
    PromiseClose.set_value(Result);
    PromiseOpen = std::promise<QUIC_STATUS>();
}
