#pragma once
#ifndef SRC_COCLASSES_HANDLE_H_
#define SRC_COCLASSES_HANDLE_H_

#include <algorithm>
#include <new>
#include <coroutine>

namespace cocls {

class abstract_resumable_t {
public:
    virtual ~abstract_resumable_t() = default;
    virtual std::coroutine_handle<> resume() noexcept = 0;
};

using resumable_handle_t = abstract_resumable_t *;


class noop_resumable_t: public abstract_resumable_t {
public:
    virtual std::coroutine_handle<> resume() noexcept override {return std::noop_coroutine();}
    static resumable_handle_t get_handle() {
        static noop_resumable_t x;
        return &x;
    }
};

template<typename Fn>
class cb_resumable_t: public abstract_resumable_t {
public:
    cb_resumable_t(Fn &&cb):_cb(std::forward<Fn>(cb)) {}
    virtual std::coroutine_handle<> resume() noexcept override {
        if constexpr (std::is_void<decltype(_cb())>::value) {
            _cb();
            return std::noop_coroutine();
        } else {
            return _cb();
        }
    }
protected:
    Fn _cb;
};


///Replacement of std::coroutine_handle for resume purpose
/**
 * This is strong concept which allows you to await on "callback". This also allows you to use
 * awaiters in non-coroutine code. You can make awaiter proxies, where instead of resuming the
 * coroutine, the callback is called and then you can resume the coroutine manually.
 * 
 */
class handle_t {
public:
    
    handle_t():_type(&obj_table), _object_ptr(nullptr) {}
    handle_t(std::coroutine_handle<> h): _type(&coro_table), _coro_handle(h) {}
    template<typename T>
    handle_t(std::coroutine_handle<T> h): _type(&coro_table), _coro_handle(h) {}
    handle_t(abstract_resumable_t *ptr): _type(&obj_table), _object_ptr(ptr) {}
    ~handle_t() {
        _type->destroy(*this);
    }
    handle_t(const handle_t &x):_type(x._type) {
        _type->create(*this,x);
    }
    operator bool() const {
        return _type->is_defined(*this);
    }
    bool operator !() const {
        return !_type->is_defined(*this);
    }
    
    handle_t &operator=(const handle_t &x) {
        if (&x != this) {
            _type->destroy(*this);
            _type = x._type;
            _type->create(*this, x);
        }
        return *this;
    }
    void resume() const {
        _type->resume(*this);
    }
    
    ///Resolves handle to coroutine handle
    /**
     * If the handle is coroutine handle it returns directly it value. If the handle is resumable object
     * it resumes it and if the object returns coroutine handle, it returns ite as result. which allows
     * to proxy resumbtion by the handle. 
     * 
     * @return coroutine handle to resume.
     * 
     * @note don't call this more then once per life (similar to resume())
     */
    std::coroutine_handle<> resume_handle() const {
        return _type->coro_handle(*this);
    }
/*
    operator std::coroutine_handle<>() const {
        return _type->coro_handle(*this);
    }

    template<typename T>
    operator std::coroutine_handle<T>() const {
        std::coroutine_handle<> h = _type->coro_handle(*this);
        void *addr = h.address();
        return std::coroutine_handle<T>::from_address(addr);
    }
    operator resumable_handle_t() const {
        if (_type->is_coro) return noop_resumable_t::get_handle();
        else return _object_ptr;
    }
  */  
    bool is_coro() const {
        return _type->is_coro;
    }
    
    ///Return address - can be use to identify object
    /**
     * don't use this addres to obtain coroutine handle
     */
    const void *address() const {
        return _type->address(*this);
    }
    
protected:
    struct fn_table {
        void (*resume)(const handle_t &);
        void (*destroy)(handle_t &);
        void (*create)(handle_t &, const handle_t &);
        const void *(*address)(const handle_t &);
        std::coroutine_handle<> (*coro_handle)(const handle_t &);
        bool (*is_defined)(const handle_t &);
        bool is_coro;
    };

    const fn_table *_type;
    union {
        std::coroutine_handle<> _coro_handle;
        abstract_resumable_t * _object_ptr;
    };

    static void coro_resume(const handle_t &me) {
        me._coro_handle.resume();
    }
    static void coro_destroy(handle_t &me) {
        me._coro_handle.~coroutine_handle<>();
    }
    static void coro_create(handle_t &me, const handle_t &src) {
        new(&me._coro_handle) std::coroutine_handle<>(src._coro_handle);
    }
    static const void *coro_address(const handle_t &me) {
        return me._coro_handle.address();
    }
    static std::coroutine_handle<> coro_coro_handle(const handle_t &me) {
        return me._coro_handle;
    }
    static bool coro_is_defined(const handle_t &me) {
        return me._coro_handle != nullptr;
    }
    static void obj_resume(const handle_t &me) {
        me._object_ptr->resume().resume();
    }
    static void obj_destroy(handle_t &me) {}

    static void obj_create(handle_t &me, const handle_t &src) {
        me._object_ptr = src._object_ptr;
    }
    static const void *obj_address(const handle_t &me) {
        return me._object_ptr;
    }
    static std::coroutine_handle<> obj_coro_handle(const handle_t &me) {
        return me._object_ptr->resume();
    }
    static bool obj_is_defined(const handle_t &me) {
        return me._object_ptr;
    }
    
    
    static constexpr fn_table coro_table {
            &coro_resume,
            &coro_destroy,
            &coro_create,
            &coro_address,
            &coro_coro_handle,
            &coro_is_defined,
            true
    };
    static constexpr fn_table obj_table {
            &obj_resume,
            &obj_destroy,
            &obj_create,
            &obj_address,
            &obj_coro_handle,
            &obj_is_defined,
            false
    };

};

}



#endif /* SRC_COCLASSES_HANDLE_H_ */
