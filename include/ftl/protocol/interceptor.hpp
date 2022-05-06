/**
 * @file interceptor.hpp
 * @copyright Copyright (c) 2022 University of Turku, MIT License
 * @author Nicolas Pope
 */

#pragma once

#include <ftl/protocol/streams.hpp>

#include <list>

namespace ftl {
namespace protocol {

/**
 * Allow packet interception by a callback between two other streams.
 */
class Intercept : public Stream {
	public:
	explicit Intercept();
	virtual ~Intercept();

	void setStream(Stream *);

	//bool onPacket(const StreamCallback &) override;
	bool onIntercept(const StreamCallback &);

	bool post(const ftl::protocol::StreamPacket &, const ftl::protocol::Packet &) override;

	bool begin() override;
	bool end() override;
	bool active() override;

	void reset() override;

	private:
	Stream *stream_;
	std::list<ftl::Handle> handles_;
	//StreamCallback cb_;
	StreamCallback intercept_;
	SHARED_MUTEX mutex_;
};

}
}
