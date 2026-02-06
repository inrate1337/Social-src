#pragma once

#include <entt/core/type_info.hpp>

#include <algorithm>
#include <functional>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <vector>

/*
 * nes - Nuvola Event System
 */
namespace nes {
    namespace detail {
        // Utils to extract type information
        template<typename class_t = std::false_type>
        struct extract_type {
            typedef class_t type;
        };

        template<typename return_t, typename class_t, typename... args_t>
        struct extract_type<return_t(class_t::*)(args_t...)> {
            typedef class_t type;
            typedef return_t ret;
        };

        enum class standard_event_priority {
            FIRST,
            NORMAL,
            LAST
        };

        struct dispatcher_base
        {
            virtual ~dispatcher_base() = default;
            virtual void deafen_instance(void* instance) = 0;
        };

        inline std::mutex& dispatcher_registry_mutex()
        {
            static std::mutex m;
            return m;
        }

        inline std::vector<dispatcher_base*>& dispatcher_registry()
        {
            static std::vector<dispatcher_base*> r;
            return r;
        }

        inline void register_dispatcher(dispatcher_base* d)
        {
            std::lock_guard<std::mutex> lock(dispatcher_registry_mutex());
            dispatcher_registry().push_back(d);
        }
    } // namespace detail

    template<typename priority_t> 
    struct event_priority_traits { using priority_type = priority_t; };

#ifndef NES_PRIORITY_TYPE
    using event_priority = detail::standard_event_priority;
    template<> 
    struct event_priority_traits<event_priority> { 
        using priority_type = event_priority; 
        static constexpr priority_type default_value = priority_type::NORMAL; 
    };
#else
    using event_priority = NES_PRIORITY_TYPE;
#ifndef NES_PRIORITY_TRAITS
    template<> 
    struct event_priority_traits<event_priority> { 
        using priority_type = event_priority; 
        static constexpr priority_type default_value = priority_type::NORMAL; 
    };
#else
    // Define your own priority traits
    NES_PRIORITY_TRAITS;
#endif
#endif

    // Owns an event pointer, used to pass around an event
    template<typename event_t>
    struct event_holder {
        template<typename... args_t>
        explicit event_holder(args_t... args) : mEvent(args...) {}

        event_t* get() {
            return &mEvent;
        }
        event_t& ref() {
            return mEvent;
        }
        event_t* operator->() {
            return get();
        }

    private:
        event_t mEvent;
    };

    // Creates an event holder instance
    template<typename event_t, typename... args_t>
    event_holder<event_t> make_holder(args_t... args) {
        return event_holder<event_t>(args...);
    }

    // A type for holding the wrapper function that invokes the callback
    template<typename event_t>
    using event_wrapper = std::function<void(event_t&)>;

    template<typename event_t, typename wrapper_t = event_wrapper<event_t>>
    struct event_listener {
        using holder_t = event_holder<event_t>;

        event_listener() = delete;
        event_listener(void* instance, wrapper_t&& wrapper, entt::id_type methodHash) 
            : mInstance(instance), mMethod(std::move(wrapper)), mMethodHash(methodHash) {}

        void invoke(holder_t& holder) const {
            mMethod(holder.ref());
        }

        void* mInstance = nullptr;
        wrapper_t mMethod{};
        entt::id_type mMethodHash{};
    };

    template<typename base_iterator>
    struct listener_list_concurrent_iterator {
        using iterator_category = typename base_iterator::iterator_category;
        using difference_type = typename base_iterator::difference_type;
        using value_type = typename base_iterator::value_type;
        using pointer = typename base_iterator::pointer;
        using reference = typename base_iterator::reference;

        listener_list_concurrent_iterator(base_iterator base, std::shared_lock<std::shared_mutex>&& writeLock) 
            : mBase(base), mWriteLock(std::move(writeLock)) {}

        reference operator*() const { return *mBase; };
        pointer operator->() { return mBase; };
        auto& operator++() {
            ++mBase;
            return *this;
        };
        auto operator++(int) const { return listener_list_concurrent_iterator{++mBase, this->mWriteLock}; };

        friend bool operator==(const listener_list_concurrent_iterator& a, const listener_list_concurrent_iterator& b) { 
            return a.mBase == b.mBase; 
        };
        friend bool operator!=(const listener_list_concurrent_iterator& a, const listener_list_concurrent_iterator& b) { 
            return a.mBase != b.mBase; 
        };

    private:
        base_iterator mBase;
        std::shared_lock<std::shared_mutex> mWriteLock;
    };

    template<typename listener_t>
    struct listener_list {
        using container_type = std::vector<listener_t>;
        using iterator = listener_list_concurrent_iterator<typename container_type::iterator>;

        iterator begin() { return iterator(mListeners.begin(), std::shared_lock(mWriteLock)); }
        iterator end() { return iterator(mListeners.end(), std::shared_lock(mWriteLock)); }

        bool contains(void* instance, entt::id_type methodHash) const
        {
            std::shared_lock readLock(mWriteLock);
            return std::any_of(mListeners.begin(), mListeners.end(), [&](const listener_t& listener) {
                return listener.mInstance == instance && listener.mMethodHash == methodHash;
            });
        }

        template<typename... args_t>
        auto emplace_back(args_t&&... args) {
            std::unique_lock writeLock(mWriteLock);
            return mListeners.emplace_back(std::forward<args_t>(args)...);
        }

        template<typename... args_t>
        auto erase_if(args_t&&... args) {
            std::unique_lock writeLock(mWriteLock);
            return std::erase_if(mListeners, std::forward<args_t>(args)...);
        }

    private:
        container_type mListeners;
        mutable std::shared_mutex mWriteLock;
    };

    template<typename event_t>
    struct dispatcher : detail::dispatcher_base {
        using listener_t = event_listener<event_t>;
        using holder_t = event_holder<event_t>;

        template<auto handler, auto priority = event_priority_traits<event_priority>::default_value, 
                 typename class_t = detail::extract_type<decltype(handler)>::class_t, 
                 typename wrapper_t = event_wrapper<event_t>>
        void listen(class_t* instance) {
            std::unique_lock lock(mMutex);
            const auto methodHash = entt::type_hash<decltype(handler)>::value();
            if (mListeners[priority].contains(instance, methodHash)) return;
            wrapper_t wrapper = [instance](event_t& e) {
                (instance->*handler)(e);
            };
            mListeners[priority].emplace_back(instance, std::move(wrapper), methodHash);
        }

        template<auto priority = event_priority_traits<event_priority>::default_value, typename wrapper_t = event_wrapper<event_t>>
        void listen(auto handler) {
            std::unique_lock lock(mMutex);
            const auto methodHash = entt::type_hash<decltype(handler)>::value();
            if (mListeners[priority].contains(nullptr, methodHash)) return;
            wrapper_t wrapper = [handler](event_t& e) {
                handler(e);
            };
            mListeners[priority].emplace_back(nullptr, std::move(wrapper), methodHash);
        }

        template<typename handler_t, typename class_t = detail::extract_type<handler_t>::class_t>
        void deafen(class_t* instance, handler_t&& handler) {
            std::unique_lock lock(mMutex);
            const auto methodHash = entt::type_hash<handler_t>::value();
            for (auto& [priority, listeners] : mListeners) {
                listeners.erase_if([&](auto& listener) -> bool { 
                    return listener.mInstance == instance && listener.mMethodHash == methodHash;
                });
            }
        }

        void deafen_instance(void* instance) override
        {
            std::unique_lock lock(mMutex);
            for (auto& [priority, listeners] : mListeners) {
                listeners.erase_if([&](auto& listener) -> bool {
                    return listener.mInstance == instance;
                });
            }
        }

        void trigger(holder_t& holder) {
            std::shared_lock lock(mMutex);
            for (auto& [priority, listeners] : mListeners) {
                for (const auto& listener : listeners) {
                    listener.invoke(holder);
                }
            }
        }

    private:
        std::map<event_priority, listener_list<listener_t>> mListeners;
        mutable std::shared_mutex mMutex;
    };

    // The main event dispatcher, use this to listen for and dispatch events
    struct event_dispatcher {
        template<typename event_t>
        [[nodiscard]] auto& get() const {
            static nes::dispatcher<event_t> instance;
            static bool registered = ([]() {
                nes::detail::register_dispatcher(&instance);
                return true;
            })();
            (void)registered;
            return instance;
        }

        template<typename event_t>
        void trigger(nes::event_holder<event_t>& e) const {
            auto& disp = get<event_t>();
            disp.trigger(e);
        }

        template<typename event_t, auto handler, auto priority = nes::event_priority_traits<event_priority>::default_value, 
                 typename class_t = nes::detail::extract_type<decltype(handler)>::class_t>
        void listen(class_t* instance) const {
            auto& disp = get<event_t>();
            disp.template listen<handler, priority>(instance);
        }

        template<typename event_t, auto priority = nes::event_priority_traits<event_priority>::default_value>
        void listen(auto handler) const {
            auto& disp = get<event_t>();
            disp.template listen<priority>(handler);
        }

        template<typename event_t, auto handler, typename class_t = nes::detail::extract_type<decltype(handler)>::class_t>
        void deafen(class_t* instance) const {
            auto& disp = get<event_t>();
            disp.deafen(instance, static_cast<decltype(handler)>(handler));
        }

        template<typename event_t>
        void deafen(auto handler) const {
            auto& disp = get<event_t>();
            disp.deafen(handler);
        }

        void deafenAll(void* instance) const
        {
            if (!instance) return;
            std::lock_guard<std::mutex> lock(nes::detail::dispatcher_registry_mutex());
            for (auto* d : nes::detail::dispatcher_registry())
            {
                if (d) d->deafen_instance(instance);
            }
        }
    };
} // namespace nes
