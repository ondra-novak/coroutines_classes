/*
 * function.h
 *
 *  Created on: 28. 2. 2023
 *      Author: ondra
 */

#ifndef SRC_COCLASSES_FUNCTION_H_
#define SRC_COCLASSES_FUNCTION_H_
#include "common.h"
#include <type_traits>
#include <functional>

namespace cocls {

///Replacement for std::function
/** supports movable functions (similar to move_only_function)
 *  supports small object optimization
 *
 *  @tparam Fn function prototype Ret<Args>
 *  @tparam small_space specify what how many bytes is small object, Default is
 *  8xsize of pointer, which is 64bytes for x64 and 32 bytes for x32. Note that
 *  space for the function itself is 1 pointer less, as there is always vtptr
 *  which is always a pointer.
 */
template<typename Fn, std::size_t small_space = sizeof(void *)*8> class function;

template<typename T>
using type_to_cref = std::conditional_t<std::is_reference_v<T>,T,const T &>;


template<typename T>
struct is_function_t {static constexpr bool value = false;};
template<typename T, std::size_t sz>
struct is_function_t<function<T, sz> > {static constexpr bool value = true;};

///determines, whether T is cocls::function
template<typename T>
inline constexpr bool is_function = is_function_t<T>::value;

template< std::size_t small_space, bool e, typename Ret, typename ... Args>
class function<Ret(Args...) noexcept(e), small_space> {
public:

    ///type of function
    using T = Ret(Args...) noexcept(e);

    ///call operation
    template<typename ... A>
    Ret operator()(Args ... args) const noexcept(e)  {
        if (_ptr) return _ptr->call(args...);
        else throw std::bad_function_call();
    }

    ///construct function without the target
    function()=default;

    ///construct function with the target
    /**
     * @param fn invokable object, cannot be created with object function
     *
     * @note the target must be movable (MoveConstructible). The function
     * can be stored in internal storage, of on heap if it too large. If it
     * is stored in internal storage, any move operation cause moving the closure
     * of the function. If the function is stored on heap. the move
     * operation just moves the pointer.
     *
     */
    template<typename Fn>
    CXX20_REQUIRES(std::invocable<Fn, Args...> && (!is_function<Fn>))
    function(Fn &&fn) {
        init(std::forward<Fn>(fn));
    }

    ///construct function object from other object with different internal storage
    /**
     * @param other source object
     *
     * function is moved. If the function doesn't fit to new storage, it is
     * allocated on heap
     */
    template<std::size_t sz>
    function(function<T, sz> &&other) {
        if (other._ptr) {
            _ptr = other._ptr->move(space, small_space);
            other._ptr = nullptr;
        }
    }

    ///copy is deleted
    function(const function &) = delete;

    ///move the target
    function(function &other):_ptr(other._ptr?other._ptr->move(space, small_space):nullptr) {
        other._ptr = nullptr;
    }

    ///assign is deleted
    function &operator=(const function &) = delete;

    ///assign by moving, which also destroyes original target
    function &operator=(function &&other) {
        if (&other != this) {
            delete _ptr;
            _ptr = other._ptr?other._ptr->move(space, small_space):nullptr;
            other._ptr = nullptr;
        }
        return *this;
    }

    ///returns true, if the object has a target
    operator bool() const {return _ptr;}
    ///returns true, if the object has no target
    bool operator!() const {return !_ptr;}

    ///compares to nullptr
    bool operator==(std::nullptr_t) {return _ptr ==nullptr;}

    ///destroys any held target
    ~function() {
        delete _ptr;
    }



protected:
    class Abstract {
    public:
        virtual Ret call(std::add_lvalue_reference_t<Args> ... args) noexcept(e) = 0;
        virtual Abstract *move(void *newplace, std::size_t sz) = 0;
        virtual ~Abstract() = default;
    };

    template<typename Fn>
    class FnInst: public Abstract {
    public:
        FnInst(Fn &&fn):_fn(std::forward<Fn>(fn)) {}
        virtual Ret call(std::add_lvalue_reference_t<Args> ... args) noexcept(e) {
            return _fn(std::forward<Args>(args)...);
        }
        virtual Abstract *move(void *newplace, std::size_t sz) {return this;}
    protected:
        Fn _fn;
    };

    template<typename Fn>
    class FnInstSmall: public FnInst<Fn> {
    public:
        FnInstSmall(Fn &&fn):FnInst<Fn>(std::forward<Fn>(fn)) {}
        virtual Abstract *move(void *newplace, std::size_t sz) {
            Abstract *out;
            if (sz >= sizeof(FnInstSmall<Fn>)) {
                out  = new(newplace) FnInstSmall(std::move(*this));
            } else {
                out = new FnInst<Fn>(std::forward<Fn>(FnInst<Fn>::_fn));
            }
            delete this;
            return out;
        }
        void *operator new(std::size_t, void *p) {return p;}
        void operator delete (void *, void *p) {}
        void operator delete (void *, std::size_t) {}
    };

    template<typename Fn>
    void init(Fn &&fn) {
        if constexpr (sizeof(FnInstSmall<Fn>) <= small_space) {
            _ptr = new(space) FnInstSmall<Fn>(std::forward<Fn>(fn));
        } else {
            _ptr = new FnInst<Fn>(std::forward<Fn>(fn));
        }
    }

    Abstract *_ptr = nullptr;
    char space[small_space];


};

}



#endif /* SRC_COCLASSES_FUNCTION_H_ */
