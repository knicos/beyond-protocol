#pragma once

#include <string>
#include <vector>

struct CertificateParams
{
    /// @brief PKCS12 certificate name
    std::string certificate_name = "";

    /// @brief country
    std::string C = "";
    /// @brief organization
    std::string O = "";
    /// @brief  common name
    std::string CN = "localhost";
};

// Methods for generating self signed certificates with private key (RSA2046/SHA256). Should only be used for unit tests

// For MsQuicConfiguration::SetCertificateFiles()
bool create_self_signed_certificate_files(const CertificateParams& params, const std::string& certificate_path, const std::string& private_key_path);

// For MsQuicConfiguration::SetCertificatePKCS12()
bool create_self_signed_certificate_pkcs12(const CertificateParams& params, std::vector<unsigned char>& blob);

std::string get_certificate_signature_base64(const char* data, int len); // DER binary blob
std::string get_certificate_signature_base64(void* /* X509* */ cert); // OpenSSL X509*

std::string get_certificate_info(const char* data, int len); // DER binary blob
std::string get_certificate_info(void* /* X509* */ cert); // OpenSSL X509*
