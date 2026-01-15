#ifndef NANO_SIGNAL_SLOT_HPP
#define NANO_SIGNAL_SLOT_HPP

#include "nano_function.hpp"
#include "nano_observer.hpp"

#include <memory>
#include <type_traits>

namespace Nano
{

template <typename RT> class Signal;
template <typename RT, typename... Args>
class Signal<RT(Args...)> : private Observer
{
    // Storage for lambdas - type-erased wrapper
    struct LambdaStorageBase
    {
        virtual ~LambdaStorageBase() = default;
        virtual void* getPtr() = 0;
    };

    template <typename L>
    struct LambdaStorage : LambdaStorageBase
    {
        L lambda;
        LambdaStorage(L&& l) : lambda(std::move(l)) {}
        void* getPtr() override { return &lambda; }
    };

    // Node for storing lambdas in a linked list
    struct LambdaNode
    {
        std::unique_ptr<LambdaStorageBase> storage;
        DelegateKey key;
        LambdaNode* next = nullptr;
    };

    LambdaNode* m_lambda_head = nullptr;

    // Helper to add lambda storage
    template <typename L>
    void* storeLambda(L&& lambda, DelegateKey const& key)
    {
        auto node = new LambdaNode();
        node->storage = std::unique_ptr<LambdaStorageBase>(
            new LambdaStorage<typename std::decay<L>::type>(std::forward<L>(lambda)));
        node->key = key;
        node->next = m_lambda_head;
        m_lambda_head = node;
        return node->storage->getPtr();
    }

    // Helper to remove lambda storage by key
    void removeLambdaStorage(DelegateKey const& key)
    {
        LambdaNode** pp = &m_lambda_head;
        while (*pp)
        {
            if ((*pp)->key == key)
            {
                LambdaNode* toDelete = *pp;
                *pp = (*pp)->next;
                delete toDelete;
                return;
            }
            pp = &((*pp)->next);
        }
    }

    // Clean up all lambda storage
    void clearLambdaStorage()
    {
        while (m_lambda_head)
        {
            LambdaNode* next = m_lambda_head->next;
            delete m_lambda_head;
            m_lambda_head = next;
        }
    }

    template <typename T>
    void insert_sfinae(DelegateKey const& key, typename T::Observer* instance)
    {
        Observer::insert(key, instance);
        instance->insert(key, this);
    }
    template <typename T>
    void remove_sfinae(DelegateKey const& key, typename T::Observer* instance)
    {
        Observer::remove(key, instance);
        instance->remove(key, this);
    }
    template <typename T>
    void insert_sfinae(DelegateKey const& key, ...)
    {
        Observer::insert(key, this);
    }
    template <typename T>
    void remove_sfinae(DelegateKey const& key, ...)
    {
        Observer::remove(key, this);
    }

    public:

    using Delegate = Function<RT(Args...)>;

    // Connection ID for lambda disconnection
    using ConnectionId = DelegateKey;

    ~Signal()
    {
        clearLambdaStorage();
    }

    // Non-copyable, non-movable (due to lambda storage with raw pointers in observer)
    Signal() = default;
    Signal(const Signal&) = delete;
    Signal& operator=(const Signal&) = delete;
    Signal(Signal&&) = delete;
    Signal& operator=(Signal&&) = delete;

    //-------------------------------------------------------------------CONNECT

    // Connect a lambda/callable by value (takes ownership)
    // Returns a ConnectionId that can be used to disconnect later
    template <typename L>
    typename std::enable_if<
        !std::is_pointer<typename std::decay<L>::type>::value &&
        !std::is_lvalue_reference<L>::value,
        ConnectionId
    >::type
    connect(L&& lambda)
    {
        using DecayedL = typename std::decay<L>::type;
        // Create a temporary to get the delegate key structure
        // (we need the thunk pointer which is always the same for this lambda type)
        DecayedL temp = std::forward<L>(lambda);
        DelegateKey key = Delegate::template bind<DecayedL>(&temp);
        // Now store the actual lambda and update the key with its real address
        void* storedPtr = storeLambda(std::move(temp), key);
        key[0] = reinterpret_cast<std::uintptr_t>(storedPtr);
        // Update the stored key to match the actual address
        m_lambda_head->key = key;
        Observer::insert(key, this);
        return key;
    }

    template <typename L>
    void connect(L* instance)
    {
        Observer::insert(Delegate::template bind<L>(instance), this);
    }
    template <typename L>
    void connect(L& instance)
    {
        connect(std::addressof(instance));
    }

    template <RT (* fun_ptr)(Args...)>
    void connect()
    {
        Observer::insert(Delegate::template bind<fun_ptr>(), this);
    }

    template <typename T, RT (T::* mem_ptr)(Args...)>
    void connect(T* instance)
    {
        insert_sfinae<T>(Delegate::template bind<T, mem_ptr>(instance), instance);
    }
    template <typename T, RT (T::* mem_ptr)(Args...) const>
    void connect(T* instance)
    {
        insert_sfinae<T>(Delegate::template bind<T, mem_ptr>(instance), instance);
    }

    template <typename T, RT (T::* mem_ptr)(Args...)>
    void connect(T& instance)
    {
        connect<T, mem_ptr>(std::addressof(instance));
    }
    template <typename T, RT (T::* mem_ptr)(Args...) const>
    void connect(T& instance)
    {
        connect<T, mem_ptr>(std::addressof(instance));
    }
    
    //----------------------------------------------------------------DISCONNECT

    // Disconnect a lambda using its ConnectionId
    void disconnect(ConnectionId const& id)
    {
        Observer::remove(id, this);
        removeLambdaStorage(id);
    }

    template <typename L>
    typename std::enable_if<
        !std::is_same<typename std::decay<L>::type, DelegateKey>::value
    >::type
    disconnect(L* instance)
    {
        Observer::remove(Delegate::template bind<L>(instance), this);
    }
    template <typename L>
    typename std::enable_if<
        !std::is_same<typename std::decay<L>::type, DelegateKey>::value
    >::type
    disconnect(L& instance)
    {
        disconnect(std::addressof(instance));
    }

    template <RT (* fun_ptr)(Args...)>
    void disconnect()
    {
        Observer::remove(Delegate::template bind<fun_ptr>(), this);
    }
    
    template <typename T, RT (T::* mem_ptr)(Args...)>
    void disconnect(T* instance)
    {
        remove_sfinae<T>(Delegate::template bind<T, mem_ptr>(instance), instance);
    }
    template <typename T, RT (T::* mem_ptr)(Args...) const>
    void disconnect(T* instance)
    {
        remove_sfinae<T>(Delegate::template bind<T, mem_ptr>(instance), instance);
    }

    template <typename T, RT (T::* mem_ptr)(Args...)>
    void disconnect(T& instance)
    {
        disconnect<T, mem_ptr>(std::addressof(instance));
    }
    template <typename T, RT (T::* mem_ptr)(Args...) const>
    void disconnect(T& instance)
    {
        disconnect<T, mem_ptr>(std::addressof(instance));
    }
    
    //----------------------------------------------------EMIT / EMIT ACCUMULATE

    #ifdef NANO_USE_DEPRECATED

    /// Will not benefit from perfect forwarding
    /// TODO [[deprecated]] when c++14 is comfortably supported

    void operator() (Args... args)
    {
        emit(std::forward<Args>(args)...);
    }
    template <typename Accumulate>
    void operator() (Args... args, Accumulate&& accumulate)
    {
        emit_accumulate<Accumulate>
            (std::forward<Accumulate>(accumulate), std::forward<Args>(args)...);
    }

    #endif

    template <typename... Uref>
    void emit(Uref&&... args)
    {
        Observer::onEach<Delegate>(std::forward<Uref>(args)...);
    }

    template <typename Accumulate, typename... Uref>
    void emit_accumulate(Accumulate&& accumulate, Uref&&... args)
    {
        Observer::onEach_Accumulate<Delegate, Accumulate>
            (std::forward<Accumulate>(accumulate), std::forward<Uref>(args)...);
    }

    //-------------------------------------------------------------------UTILITY

    bool empty() const
    {
        return Observer::isEmpty();
    }

    void removeAll()
    {
        Observer::removeAll();
        clearLambdaStorage();
    }

};

} // namespace Nano ------------------------------------------------------------

#endif // NANO_SIGNAL_SLOT_HPP
