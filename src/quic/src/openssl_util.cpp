#include "openssl_util.hpp"

#include <ftl/protocol/config.h>
#include <loguru.hpp>

#ifdef HAVE_OPENSSL

#include <openssl/x509.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/pkcs12.h>
#include <openssl/asn1.h>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const std::string base64_chars =
             "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
             "abcdefghijklmnopqrstuvwxyz"
             "0123456789+/";


static inline bool is_base64(char c) {
  return (isalnum(c) || (c == '+') || (c == '/'));
}

std::string base64_encode(char const* buf, unsigned int bufLen) {
    std::string ret;
    int i = 0;
    int j = 0;
    char char_array_3[3];
    char char_array_4[4];

    while (bufLen--) {
        char_array_3[i++] = *(buf++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for(i = 0; (i < 4) ; i++)
                ret += base64_chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i) {
        for(j = i; j < 3; j++)
            char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;

        for (j = 0; (j < i + 1); j++)
            ret += base64_chars[char_array_4[j]];

        while((i++ < 3))
            ret += '=';
    }

    return ret;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

X509* create_certificate(const CertificateParams& params, EVP_PKEY* pkey)
{
    X509* cert = X509_new();
    if (!cert) { return nullptr; }
    
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);

    X509_gmtime_adj(X509_getm_notBefore(cert), 0);
    X509_gmtime_adj(X509_getm_notAfter(cert), 31536000L); // 60*60*24*365
    X509_set_pubkey(cert, pkey);

    X509_NAME* name = X509_get_subject_name(cert);
    X509_NAME_add_entry_by_txt(name, "C",  MBSTRING_ASC,
                            (unsigned char *)params.C.c_str(), -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "O",  MBSTRING_ASC,
                            (unsigned char *)params.O.c_str(), -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                            (unsigned char *)params.CN.c_str(), -1, -1, 0);

    X509_set_issuer_name(cert, name);

    return cert;
}

bool create_self_signed_certificate_files(const CertificateParams& params, const std::string& certificate_path, const std::string& private_key_path)
{
    EVP_PKEY* pkey = EVP_RSA_gen(2048);
    if (!pkey) { return false; }
    X509* cert = create_certificate(params, pkey);
    if (!cert) { EVP_PKEY_free(pkey); return false; }

    X509_sign(cert, pkey, EVP_sha256());
    
    bool retval = true;
    {
        FILE* f = nullptr;
        f = fopen(private_key_path.c_str(), "wb");
        retval &= PEM_write_PrivateKey(f, pkey, nullptr, nullptr, 0, nullptr, nullptr);
        fclose(f);
    }
    {
        FILE* f = nullptr;
        f = fopen(certificate_path.c_str(), "wb");
        retval &= PEM_write_X509(f, cert);
        fclose(f);
    }
    
    X509_free(cert);
    EVP_PKEY_free(pkey);
    return retval;
}

bool create_self_signed_certificate_pkcs12(const CertificateParams& params, std::vector<unsigned char>& blob)
{
    EVP_PKEY* pkey = EVP_RSA_gen(2048);
    if (!pkey) { return false; }
    X509* cert = create_certificate(params, pkey);
    if (!cert) { EVP_PKEY_free(pkey); return false; }

    X509_sign(cert, pkey, EVP_sha256());

    PKCS12* pkcs12bundle = PKCS12_create(nullptr, params.certificate_name.c_str(), pkey, cert, nullptr, 0, 0, 0, 0, 0);
    bool retval = !!pkcs12bundle;
    if (pkcs12bundle)
    {
        auto len = i2d_PKCS12(pkcs12bundle, nullptr);
        unsigned char* buffer = (unsigned char*)OPENSSL_malloc(len);
        unsigned char* buffer_ptr = buffer;
        i2d_PKCS12(pkcs12bundle, &buffer_ptr);

        blob.resize(len);
        memcpy(blob.data(), buffer, len);
        OPENSSL_free(buffer);
        PKCS12_free(pkcs12bundle);
    }

    X509_free(cert);
    EVP_PKEY_free(pkey);
    return retval;
}

std::string get_certificate_signature_base64(X509* cert)
{
    const ASN1_BIT_STRING* psig = nullptr;
    const X509_ALGOR* palg = nullptr;
    X509_get0_signature(&psig, &palg, cert);

    return base64_encode((const char*)psig->data, psig->length);
}

std::string get_certificate_info(X509* cert)
{
    BIO* bio = BIO_new(BIO_s_mem());
    X509_print_ex(bio, cert, 0, 0);

    char* buffer = nullptr;
    auto size = BIO_get_mem_data(bio, &buffer);
    std::string result(buffer, size);

    BIO_free(bio);
    return result;
}

std::string get_certificate_signature_base64(void* cert)
{
    return get_certificate_signature_base64((X509*) cert);
}

std::string get_certificate_info(void* cert)
{
    return get_certificate_info((X509*) cert);
}

std::string get_certificate_signature_base64(const char* data, int len)
{
    X509* cert = d2i_X509(nullptr, (const unsigned char**) &data, len);
    auto info = get_certificate_signature_base64(cert);
    X509_free(cert);
    return info;
}

std::string get_certificate_info(const char* data, int len)
{
    X509* cert = d2i_X509(nullptr, (const unsigned char**) &data, len);
    auto info = get_certificate_info(cert);
    X509_free(cert);
    return info;
}

#else

bool create_self_signed_certificate_pkcs12(const CertificateParams& params, std::vector<unsigned char>& blob)
{
    LOG(ERROR) << "create_self_signed_certificate(): Built without OpenSSL";
    return false;
}

bool create_self_signed_certificate_files(const CertificateParams& params, const std::string& certificate_path, const std::string& private_key_path)
{
    LOG(ERROR) << "create_self_signed_certificate(): Built without OpenSSL";
    return false;
}

std::string get_certificate_signature_base64(const char* data, int len)
{
    LOG(ERROR) << "get_certificate_signature_base64(): Built without OpenSSL";
    return "get_certificate_signature_base64(): Built without OpenSSL";
}

std::string get_certificate_info(const char* data, int len)
{
    LOG(ERROR) << "get_certificate_info(): Built without OpenSSL";
    return "get_certificate_info(): Built without OpenSSL";
}

#endif
