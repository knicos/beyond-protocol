/**
 * @file dispatcher.cpp
 * @copyright Copyright (c) 2020 University of Turku, MIT License
 * @author Nicolas Pope
 */

#include <ftl/lib/loguru.hpp>
#include "dispatcher.hpp"
#include "peer.hpp"
#include <ftl/exception.hpp>
#include <iostream>

using ftl::net::Peer;
using ftl::net::Dispatcher;
using std::vector;
using std::string;
using std::optional;

std::string object_type_to_string(const msgpack::type::object_type t) {
	switch(t) {
		case msgpack::type::object_type::NIL: return "NIL";
		case msgpack::type::object_type::BOOLEAN: return "BOOLEAN";
		case msgpack::type::object_type::POSITIVE_INTEGER: return "POSITIVE_INTEGER";
		case msgpack::type::object_type::NEGATIVE_INTEGER: return "NEGATIVE_INTEGER";
		case msgpack::type::object_type::FLOAT32: return "FLOAT32";
		case msgpack::type::object_type::FLOAT64: return "FLOAT64";
		//case msgpack::type::object_type::FLOAT: return "FLOAT";
		case msgpack::type::object_type::STR: return "STR";
		case msgpack::type::object_type::BIN: return "BIN";
		case msgpack::type::object_type::ARRAY: return "ARRAY";
		case msgpack::type::object_type::MAP: return "MAP";
		case msgpack::type::object_type::EXT: return "EXT";
	}
	return "UNKNOWN";
}

vector<string> Dispatcher::getBindings() const {
	vector<string> res;
	for (auto x : funcs_) {
		res.push_back(x.first);
	}
	return res;
}

void ftl::net::Dispatcher::dispatch(Peer &s, const msgpack::object &msg) {
	switch (msg.via.array.size) {
	case 3:
		dispatch_notification(s, msg); break;
	case 4:
		dispatch_call(s, msg); break;
	default:
		throw FTL_Error("Unrecognised msgpack : " << msg.via.array.size);
	}
}

void ftl::net::Dispatcher::dispatch_call(Peer &s, const msgpack::object &msg) {
	call_t the_call;
	
	try {
		msg.convert(the_call);
	} catch(...) {
		throw FTL_Error("Bad message format");
	}

	// TODO: proper validation of protocol (and responding to it)
	auto &&type = std::get<0>(the_call);
	auto &&id = std::get<1>(the_call);
	auto &&name = std::get<2>(the_call);
	auto &&args = std::get<3>(the_call);
	// assert(type == 0);
	
	if (type == 1) {
		s._dispatchResponse(id, name, args);
	} else if (type == 0) {
		DLOG(2) << "RPC " << name << "() <- " << s.getURI();

		auto func = _locateHandler(name);

		if (func) {
			//DLOG(INFO) << "Found binding for " << name;
			try {
				auto result = (*func)(s, args); //->get();
				s._sendResponse(id, name, result->get());
			} catch (const std::exception &e) {
				throw FTL_Error("Exception when attempting to call RPC " << name << " (" << e.what() << ")");
				// FIXME: Send the error in the response.
			}
		} else {
			throw FTL_Error("No binding found for " << name);
		}
	} else {
		throw FTL_Error("Unrecognised message type: " << type);
	}
}

optional<Dispatcher::adaptor_type> ftl::net::Dispatcher::_locateHandler(const std::string &name) const {
	auto it_func = funcs_.find(name);
	if (it_func == funcs_.end()) {
		if (parent_ != nullptr) {
			return parent_->_locateHandler(name);
		} else {
			return {};
		}
	} else {
		return it_func->second;
	}
}

bool ftl::net::Dispatcher::isBound(const std::string &name) const {
	return funcs_.find(name) != funcs_.end();
}

void ftl::net::Dispatcher::dispatch_notification(Peer &s, msgpack::object const &msg) {
	notification_t the_call;
	msg.convert(the_call);

	// TODO: proper validation of protocol (and responding to it)
	// auto &&type = std::get<0>(the_call);
	// assert(type == static_cast<uint8_t>(request_type::notification));

	auto &&name = std::get<1>(the_call);
	auto &&args = std::get<2>(the_call);

	auto binding = _locateHandler(name);

	if (binding) {
		try {
			auto result = (*binding)(s, args);
		} catch (const int &e) {
			//throw "Exception in bound function";
			throw &e;
		} catch (const std::bad_cast &e) {
			std::string args_str = "";
			for (size_t i = 0; i < args.via.array.size; i++) {
				args_str += object_type_to_string(args.via.array.ptr[i].type);
				if ((i + 1) != args.via.array.size) args_str += ", ";
			}
			throw FTL_Error("Bad cast, got: " << args_str);

		} catch (const std::exception &e) {
			throw FTL_Error("Exception for '" << name << "' - " << e.what());
		}
	} else {
		throw FTL_Error("Missing handler for incoming message (" << name << ")");
	}
}

void ftl::net::Dispatcher::enforce_arg_count(std::string const &func, std::size_t found,
								   std::size_t expected) {
	if (found != expected) {
		throw FTL_Error("RPC argument missmatch for '" << func << "' - " << found << " != " << expected);
	}
}

void ftl::net::Dispatcher::enforce_unique_name(std::string const &func) {
	auto pos = funcs_.find(func);
	if (pos != end(funcs_)) {
		throw FTL_Error("RPC non unique binding for '" << func << "'");
	}
}

