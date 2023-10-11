#pragma once

#include <ftl/uri.hpp>


namespace ftl
{
namespace net
{

class Universe;

/** Interface for Universe to manage Quic Peers/Servers (ftl::net::Universe could/should be refactored to remove the
 *  dependency between net::PeerTcp and net::Universe and so that the class would be an interface only to manage
 *  RPCs and connections).
 */
class QuicUniverse {
public:
    static std::unique_ptr<QuicUniverse> Create(Universe* net);

    // Unload MsQuic
    static void Unload(bool force);

    virtual void Configure() = 0;

    virtual ~QuicUniverse() = default;

    virtual bool CanOpenUri(const ftl::URI& uri) = 0;
    virtual bool Listen(const ftl::URI& uri) = 0;
    virtual std::vector<ftl::URI> GetListeningUris() = 0;

    virtual PeerPtr Connect(const ftl::URI& uri, bool is_webservice=false) = 0;

protected:
    QuicUniverse() = default;
};

}
}
