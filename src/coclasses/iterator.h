#pragma once
#ifndef SRC_COCLASSES_ITERATOR_H_
#define SRC_COCLASSES_ITERATOR_H_
#include <iterator>


namespace cocls {


template<typename Generator>
class generator_iterator_postfix; 

template<typename Generator>
class generator_iterator {
public:
    
    /// One of the @link iterator_tags tag types@endlink.
    using iterator_category = std::input_iterator_tag;
    /// The type "pointed to" by the iterator.
    using value_type = std::remove_reference_t<decltype(std::declval<Generator>().value())>; 
    /// Distance between iterators is represented as this type.
    using difference_type = std::ptrdiff_t;
    /// This type represents a pointer-to-value_type.
    using pointer = value_type *;
    /// This type represents a reference-to-value_type.
    using reference = value_type &;
    
    generator_iterator(Generator &gen, bool fin):_gen(&gen),_next(fin) {}
    generator_iterator(Generator &gen):_gen(&gen),_next(gen.next()) {}
    
    bool operator==(const generator_iterator &other) const {
        return _gen == other._gen && _next == other._next;
    }
    bool operator!=(const generator_iterator &other) const {
        return !operator==(other);
    }
    
    generator_iterator &operator++() {
        _next = _gen->next();
        return *this;
    }
    
    reference operator*() const {
        return _gen->value();
    }
    pointer operator->() const {
        return &_gen->value();
    }

    struct storage {
        value_type _v;
        reference operator*() const {
            return _v;
        }
        pointer operator->() const {
            return &_v;
        }        
    };

    storage operator++(int) {
        storage z{std::move(_gen->value())};
        _next = _gen->next();
        return z;
    }

    
protected:
    Generator *_gen;
    bool _next;
    
};


}



#endif /* SRC_COCLASSES_ITERATOR_H_ */
