/**
 * @file uri.hpp
 * @copyright Copyright (c) 2020 University of Turku, MIT License
 * @author Nicolas Pope
 */

#pragma once

#include <uriparser/Uri.h>
#include <string>
#include <vector>
#include <map>
#include <ftl/lib/nlohmann/json_fwd.hpp>


namespace ftl {

typedef const char * uri_t;

/**
 * Universal Resource Identifier. Parse, modify, represent and generate URIs.
 */
class URI {
 public:
    /**
     * @brief Construct a new invalid URI object.
     * 
     */
    URI(): m_valid(false) {}

    /**
     * @brief Construct from a C string.
     * 
     * @param puri 
     */
    explicit URI(uri_t puri);

    /**
     * @brief Construct from a C++ STL string.
     * 
     * @param puri 
     */
    explicit URI(const std::string &puri);

    /**
     * @brief Copy constructor.
     * 
     * @param c 
     */
    explicit URI(const URI &c);

    ~URI() {};

    /**
     * @brief The URI protocol scheme.
     * 
     */
    enum scheme_t : int {
        SCHEME_NONE,
        SCHEME_TCP,
        SCHEME_UDP,
        SCHEME_FTL,      // Future Tech Lab
        SCHEME_FTL_QUIC, // FTL over QUIC
        SCHEME_HTTP,
        SCHEME_WS,
        SCHEME_WSS,
        SCHEME_IPC,
        SCHEME_FILE,
        SCHEME_OTHER,
        SCHEME_DEVICE,      // Data source
        SCHEME_GROUP,
        SCHEME_CAST,        // Broadcaster stream
        SCHEME_MUX,         // Multiplexer for streams
        SCHEME_MIRROR,      // Proxy for streams
        SCHEME_BEYOND       // Settings
    };

    /**
     * @brief Check if the URI was valid.
     * 
     * @return true 
     * @return false 
     */
    bool isValid() const { return m_valid; }

    /**
     * @brief Get the host component.
     * 
     * @return const std::string& 
     */
    const std::string &getHost() const { return m_host; }

    /**
     * @brief Get the port component.
     * 
     * @return int 
     */
    int getPort() const { return m_port; }

    /**
     * @brief Get the protocol component.
     * 
     * @return scheme_t 
     */
    scheme_t getProtocol() const { return m_proto; }

    /**
     * @brief Get the protocol component.
     * 
     * @return scheme_t 
     */
    scheme_t getScheme() const { return m_proto; }

    /**
     * @brief Get the path component.
     * 
     * @return const std::string& 
     */
    const std::string &getPath() const { return m_path; }

    /**
     * @brief Get any document fragment (after #).
     * 
     * @return const std::string& 
     */
    const std::string &getFragment() const { return m_frag; }

    /**
     * @brief Get any query component (after ?).
     * 
     * @return std::string 
     */
    std::string getQuery() const;

    /**
     * @brief Get the URI without fragment or query string.
     * 
     * @return const std::string& 
     */
    const std::string &getBaseURI() const { return m_base; }

    /**
     * @brief Check if username or password is given.
     * 
     * @return true 
     * @return false 
     */
    bool hasUserInfo() const;

    /**
     * @brief Get any username and password component.
     * 
     * @return const std::string& 
     */
    const std::string &getUserInfo() const;

    /**
     * Get the URI without query parameters, and limit path to length N.
     * If N is negative then it is taken from full path length.
     */
    std::string getBaseURI(int n) const;

    /**
     * @brief Get the URI without query but with user details.
     * 
     * @return std::string 
     */
    std::string getBaseURIWithUser() const;

    /**
     * @brief Get an individual path segment.
     * 
     * @param n from 0 to N.
     * @return std::string 
     */
    std::string getPathSegment(int n) const;

    /**
     * @brief Get the length of the path component.
     * 
     * @return size_t 
     */
    inline size_t getPathLength() const { return m_pathseg.size(); }

    /**
     * @brief Set a query attribute.
     * 
     * @param key 
     * @param value 
     */
    void setAttribute(const std::string &key, const std::string &value);

    /**
     * @brief Set a query attribute.
     * 
     * @param key 
     * @param value 
     */
    void setAttribute(const std::string &key, int value);

    /**
     * @brief Get a query string attribute.
     * 
     * @tparam T 
     * @param key 
     * @return T 
     */
    template <typename T>
    T getAttribute(const std::string &key) const {
        auto i = m_qmap.find(key);
        return (i != m_qmap.end()) ? T(i->second) : T();
    }

    /**
     * @brief Check if the query string includes an attribute.
     * 
     * @param a 
     * @return true 
     * @return false 
     */
    bool hasAttribute(const std::string &a) const { return m_qmap.count(a) > 0; }

    /**
     * @brief Convert back to a URI string.
     * 
     * @return std::string 
     */
    std::string to_string() const;

    /**
     * @brief If a file URI, generate an OS file path. On Windows this deals with the
     * required path translation involving drive letters. On Linux this is the same as
     * calling `getPath()`.
     * 
     * @return std::string 
     */
    std::string toFilePath() const;

    /**
     * @brief Populate a JSON object with the query attributes.
     * 
     */
    void to_json(nlohmann::json &) const;

 private:
    void _parse(uri_t puri);

    bool m_valid;
    std::string m_host;
    std::string m_path;
    std::string m_frag;
    std::string m_base;
    std::string m_userinfo;
    std::vector<std::string> m_pathseg;
    int m_port = 0;
    scheme_t m_proto = scheme_t::SCHEME_NONE;
    std::string m_protostr;
    // std::string m_query;
    std::map<std::string, std::string> m_qmap;
};

template <>
inline int URI::getAttribute<int>(const std::string &key) const {
    auto i = m_qmap.find(key);
    return (i != m_qmap.end()) ? std::stoi(i->second) : 0;
}

template <>
inline std::string URI::getAttribute<std::string>(const std::string &key) const {
    auto i = m_qmap.find(key);
    return (i != m_qmap.end()) ? i->second : "";
}

}  // namespace ftl
