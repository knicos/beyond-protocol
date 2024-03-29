/**
 * @file dispatcher.hpp
 * @copyright Copyright (c) 2020 University of Turku, MIT License
 * @author Nicolas Pope
 */

#pragma once

#include <memory>
#include <tuple>
#include <functional>
#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <optional>
#include <utility>

#include "func_traits.hpp"

#include <ftl/threads.hpp>

#include <msgpack.hpp>

namespace ftl {

namespace net {
class PeerBase;
}

namespace internal {
    //! \brief Calls a functor with argument provided directly
    template <typename Functor, typename Arg>
    auto call(Functor f, Arg &&arg)
        -> decltype(f(std::forward<Arg>(arg))) {
        return f(std::forward<Arg>(arg));
    }

    template <typename Functor, typename... Args, std::size_t... I>
    decltype(auto) call_helper(Functor func, std::tuple<Args...> &&params,
                               std::index_sequence<I...>) {
        return func(std::get<I>(params)...);
    }

    template <typename Functor, typename... Args, std::size_t... I>
    decltype(auto) call_helper(Functor func, ftl::net::PeerBase &p, std::tuple<Args...> &&params,
                               std::index_sequence<I...>) {
        return func(p, std::get<I>(params)...);
    }

    //! \brief Calls a functor with arguments provided as a tuple
    template <typename Functor, typename... Args>
    decltype(auto) call(Functor f, std::tuple<Args...> &args) {
        return call_helper(f, std::forward<std::tuple<Args...>>(args),
                           std::index_sequence_for<Args...>{});
    }

    //! \brief Calls a functor with arguments provided as a tuple
    template <typename Functor, typename... Args>
    decltype(auto) call(Functor f, ftl::net::PeerBase &p, std::tuple<Args...> &args) {
        return call_helper(f, p, std::forward<std::tuple<Args...>>(args),
                           std::index_sequence_for<Args...>{});
    }
}

namespace net {

/**
 * Allows binding and dispatching of RPC calls. Uses type traits to generate
 * dispatch functions from the type of the binding function (return and
 * arguments). Used by ftl::net::Peer and Universe.
 */
class Dispatcher {
 public:
    explicit Dispatcher(Dispatcher *parent = nullptr) : parent_(parent) {}
    ~Dispatcher();

    /**
     * Primary method by which a peer dispatches a msgpack object that this
     * class then decodes to find correct handler and types.
     */
    void dispatch(ftl::net::PeerBase &, const msgpack::object &msg);

    // Without peer object =====================================================

    /**
     * Associate a C++ function woth a string name. Use type traits of that
     * function to build a dispatch function that knows how to decode msgpack.
     * This is the no arguments and no result case.
     */
    template <typename F>
    void bind(std::string const &name, F func,
                          ftl::internal::tags::void_result const &,
                          ftl::internal::tags::zero_arg const &,
                          ftl::internal::false_ const &) {
        enforce_unique_name(name);
        UNIQUE_LOCK(mutex_, lk);
        funcs_.insert(
            std::make_pair(name, [func, name](ftl::net::PeerBase &p, msgpack::object const &args) {
                enforce_arg_count(name, 0, args.via.array.size);
                func();
                return std::make_unique<msgpack::object_handle>();
            }));
    }

    /**
     * Associate a C++ function woth a string name. Use type traits of that
     * function to build a dispatch function that knows how to decode msgpack.
     * This is the arguments but no result case.
     */
    template <typename F>
    void bind(std::string const &name, F func,
                          ftl::internal::tags::void_result const &,
                          ftl::internal::tags::nonzero_arg const &,
                          ftl::internal::false_ const &) {
        using ftl::internal::func_traits;
        using args_type = typename func_traits<F>::args_type;

        enforce_unique_name(name);
        UNIQUE_LOCK(mutex_, lk);
        funcs_.insert(
            std::make_pair(name, [func, name](ftl::net::PeerBase &p, msgpack::object const &args) {
                constexpr int args_count = std::tuple_size<args_type>::value;
                enforce_arg_count(name, args_count, args.via.array.size);
                args_type args_real;
                args.convert(args_real);
                ftl::internal::call(func, args_real);
                return std::make_unique<msgpack::object_handle>();
            }));
    }

    /**
     * Associate a C++ function woth a string name. Use type traits of that
     * function to build a dispatch function that knows how to decode msgpack.
     * This is the no arguments but with result case.
     */
    template <typename F>
    void bind(std::string const &name, F func,
                          ftl::internal::tags::nonvoid_result const &,
                          ftl::internal::tags::zero_arg const &,
                          ftl::internal::false_ const &) {
        using ftl::internal::func_traits;

        enforce_unique_name(name);
        UNIQUE_LOCK(mutex_, lk);
        funcs_.insert(std::make_pair(name, [func,
                                            name](ftl::net::PeerBase &p, msgpack::object const &args) {
            enforce_arg_count(name, 0, args.via.array.size);
            auto z = std::make_unique<msgpack::zone>();
            auto result = msgpack::object(func(), *z);
            return std::make_unique<msgpack::object_handle>(result, std::move(z));
        }));
    }

    /**
     * Associate a C++ function woth a string name. Use type traits of that
     * function to build a dispatch function that knows how to decode msgpack.
     * This is the with arguments and with result case.
     */
    template <typename F>
    void bind(std::string const &name, F func,
                          ftl::internal::tags::nonvoid_result const &,
                          ftl::internal::tags::nonzero_arg const &,
                          ftl::internal::false_ const &) {
        using ftl::internal::func_traits;
        using args_type = typename func_traits<F>::args_type;

        enforce_unique_name(name);
        UNIQUE_LOCK(mutex_, lk);
        funcs_.insert(std::make_pair(name, [func,
                                            name](ftl::net::PeerBase &p, msgpack::object const &args) {
            constexpr int args_count = std::tuple_size<args_type>::value;
            enforce_arg_count(name, args_count, args.via.array.size);
            args_type args_real;
            args.convert(args_real);
            auto z = std::make_unique<msgpack::zone>();
            auto result = msgpack::object(ftl::internal::call(func, args_real), *z);
            return std::make_unique<msgpack::object_handle>(result, std::move(z));
        }));
    }

    // With peer object ========================================================

    template <typename F>
    void bind(std::string const &name, F func,
                          ftl::internal::tags::void_result const &,
                          ftl::internal::tags::zero_arg const &,
                          ftl::internal::true_ const &) {
        enforce_unique_name(name);
        UNIQUE_LOCK(mutex_, lk);
        funcs_.insert(
            std::make_pair(name, [func, name](ftl::net::PeerBase &p, msgpack::object const &args) {
                enforce_arg_count(name, 0, args.via.array.size);
                func(p);
                return std::make_unique<msgpack::object_handle>();
            }));
    }

    template <typename F>
    void bind(std::string const &name, F func,
                          ftl::internal::tags::void_result const &,
                          ftl::internal::tags::nonzero_arg const &,
                          ftl::internal::true_ const &) {
        using ftl::internal::func_traits;
        using args_type = typename func_traits<F>::args_type;

        enforce_unique_name(name);
        UNIQUE_LOCK(mutex_, lk);
        funcs_.insert(
            std::make_pair(name, [func, name](ftl::net::PeerBase &p, msgpack::object const &args) {
                constexpr int args_count = std::tuple_size<args_type>::value;
                enforce_arg_count(name, args_count, args.via.array.size);
                args_type args_real;
                args.convert(args_real);
                ftl::internal::call(func, p, args_real);
                return std::make_unique<msgpack::object_handle>();
            }));
    }

    template <typename F>
    void bind(std::string const &name, F func,
                          ftl::internal::tags::nonvoid_result const &,
                          ftl::internal::tags::zero_arg const &,
                          ftl::internal::true_ const &) {
        using ftl::internal::func_traits;

        enforce_unique_name(name);
        UNIQUE_LOCK(mutex_, lk);
        funcs_.insert(std::make_pair(name, [func,
                                            name](ftl::net::PeerBase &p, msgpack::object const &args) {
            enforce_arg_count(name, 0, args.via.array.size);
            auto z = std::make_unique<msgpack::zone>();
            auto result = msgpack::object(func(p), *z);
            return std::make_unique<msgpack::object_handle>(result, std::move(z));
        }));
    }

    template <typename F>
    void bind(std::string const &name, F func,
                          ftl::internal::tags::nonvoid_result const &,
                          ftl::internal::tags::nonzero_arg const &,
                          ftl::internal::true_ const &) {
        using ftl::internal::func_traits;
        using args_type = typename func_traits<F>::args_type;

        enforce_unique_name(name);
        UNIQUE_LOCK(mutex_, lk);
        funcs_.insert(std::make_pair(name, [func,
                                            name](ftl::net::PeerBase &p, msgpack::object const &args) {
            constexpr int args_count = std::tuple_size<args_type>::value;
            enforce_arg_count(name, args_count, args.via.array.size);
            args_type args_real;
            args.convert(args_real);
            auto z = std::make_unique<msgpack::zone>();
            auto result = msgpack::object(ftl::internal::call(func, p, args_real), *z);
            return std::make_unique<msgpack::object_handle>(result, std::move(z));
        }));
    }

    //==========================================================================

    /**
     * Remove a previous bound function by name.
     */
    void unbind(const std::string &name);

    /**
     * @return All bound function names.
     */
    std::vector<std::string> getBindings() const;

    /**
     * @param name Function name.
     * @return True if the given name is bound to a function.
     */
    bool isBound(const std::string &name) const;


    //==== Types ===============================================================

    using adaptor_type = std::function<std::unique_ptr<msgpack::object_handle>(
        ftl::net::PeerBase &, msgpack::object const &)>;

    //! \brief This is the type of messages as per the msgpack-rpc spec.
    using call_t = std::tuple<int8_t, uint32_t, std::string, msgpack::object>;

    //! \brief This is the type of notification messages.
    using notification_t = std::tuple<int8_t, std::string, msgpack::object>;

    using response_t =
        std::tuple<uint32_t, uint32_t, msgpack::object, msgpack::object>;

 private:
    Dispatcher *parent_;
    std::unordered_map<std::string, adaptor_type> funcs_;
    mutable SHARED_MUTEX mutex_;

    std::optional<adaptor_type> _locateHandler(const std::string &name) const;

    static void enforce_arg_count(std::string const &func, std::size_t found,
                                  std::size_t expected);

    void enforce_unique_name(std::string const &func);

    void dispatch_call(ftl::net::PeerBase &, const msgpack::object &msg);
    void dispatch_notification(ftl::net::PeerBase &, msgpack::object const &msg);
};

}  // namespace net
}  // namespace ftl
