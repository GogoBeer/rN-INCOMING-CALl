
// Copyright (c) 2015-2020 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_PREVECTOR_H
#define BITCOIN_PREVECTOR_H

#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <algorithm>
#include <cstddef>
#include <type_traits>
#include <utility>

/** Implements a drop-in replacement for std::vector<T> which stores up to N
 *  elements directly (without heap allocation). The types Size and Diff are
 *  used to store element counts, and can be any unsigned + signed type.
 *
 *  Storage layout is either:
 *  - Direct allocation:
 *    - Size _size: the number of used elements (between 0 and N)
 *    - T direct[N]: an array of N elements of type T
 *      (only the first _size are initialized).
 *  - Indirect allocation:
 *    - Size _size: the number of used elements plus N + 1
 *    - Size capacity: the number of allocated elements
 *    - T* indirect: a pointer to an array of capacity elements of type T
 *      (only the first _size are initialized).
 *
 *  The data type T must be movable by memmove/realloc(). Once we switch to C++,
 *  move constructors can be used instead.
 */
template<unsigned int N, typename T, typename Size = uint32_t, typename Diff = int32_t>
class prevector {
public:
    typedef Size size_type;
    typedef Diff difference_type;
    typedef T value_type;
    typedef value_type& reference;
    typedef const value_type& const_reference;
    typedef value_type* pointer;
    typedef const value_type* const_pointer;

    class iterator {
        T* ptr;
    public:
        typedef Diff difference_type;
        typedef T value_type;
        typedef T* pointer;
        typedef T& reference;
        typedef std::random_access_iterator_tag iterator_category;
        iterator(T* ptr_) : ptr(ptr_) {}
        T& operator*() const { return *ptr; }
        T* operator->() const { return ptr; }
        T& operator[](size_type pos) { return ptr[pos]; }
        const T& operator[](size_type pos) const { return ptr[pos]; }
        iterator& operator++() { ptr++; return *this; }
        iterator& operator--() { ptr--; return *this; }
        iterator operator++(int) { iterator copy(*this); ++(*this); return copy; }
        iterator operator--(int) { iterator copy(*this); --(*this); return copy; }
        difference_type friend operator-(iterator a, iterator b) { return (&(*a) - &(*b)); }
        iterator operator+(size_type n) { return iterator(ptr + n); }
        iterator& operator+=(size_type n) { ptr += n; return *this; }
        iterator operator-(size_type n) { return iterator(ptr - n); }
        iterator& operator-=(size_type n) { ptr -= n; return *this; }
        bool operator==(iterator x) const { return ptr == x.ptr; }
        bool operator!=(iterator x) const { return ptr != x.ptr; }
        bool operator>=(iterator x) const { return ptr >= x.ptr; }
        bool operator<=(iterator x) const { return ptr <= x.ptr; }
        bool operator>(iterator x) const { return ptr > x.ptr; }
        bool operator<(iterator x) const { return ptr < x.ptr; }
    };

    class reverse_iterator {
        T* ptr;
    public:
        typedef Diff difference_type;
        typedef T value_type;
        typedef T* pointer;
        typedef T& reference;
        typedef std::bidirectional_iterator_tag iterator_category;
        reverse_iterator(T* ptr_) : ptr(ptr_) {}
        T& operator*() { return *ptr; }
        const T& operator*() const { return *ptr; }
        T* operator->() { return ptr; }
        const T* operator->() const { return ptr; }
        reverse_iterator& operator--() { ptr++; return *this; }
        reverse_iterator& operator++() { ptr--; return *this; }
        reverse_iterator operator++(int) { reverse_iterator copy(*this); ++(*this); return copy; }
        reverse_iterator operator--(int) { reverse_iterator copy(*this); --(*this); return copy; }
        bool operator==(reverse_iterator x) const { return ptr == x.ptr; }
        bool operator!=(reverse_iterator x) const { return ptr != x.ptr; }
    };

    class const_iterator {
        const T* ptr;
    public:
        typedef Diff difference_type;
        typedef const T value_type;
        typedef const T* pointer;
        typedef const T& reference;
        typedef std::random_access_iterator_tag iterator_category;
        const_iterator(const T* ptr_) : ptr(ptr_) {}
        const_iterator(iterator x) : ptr(&(*x)) {}
        const T& operator*() const { return *ptr; }
        const T* operator->() const { return ptr; }
        const T& operator[](size_type pos) const { return ptr[pos]; }
        const_iterator& operator++() { ptr++; return *this; }
        const_iterator& operator--() { ptr--; return *this; }
        const_iterator operator++(int) { const_iterator copy(*this); ++(*this); return copy; }
        const_iterator operator--(int) { const_iterator copy(*this); --(*this); return copy; }
        difference_type friend operator-(const_iterator a, const_iterator b) { return (&(*a) - &(*b)); }
        const_iterator operator+(size_type n) { return const_iterator(ptr + n); }
        const_iterator& operator+=(size_type n) { ptr += n; return *this; }
        const_iterator operator-(size_type n) { return const_iterator(ptr - n); }
        const_iterator& operator-=(size_type n) { ptr -= n; return *this; }
        bool operator==(const_iterator x) const { return ptr == x.ptr; }
        bool operator!=(const_iterator x) const { return ptr != x.ptr; }
        bool operator>=(const_iterator x) const { return ptr >= x.ptr; }
        bool operator<=(const_iterator x) const { return ptr <= x.ptr; }
        bool operator>(const_iterator x) const { return ptr > x.ptr; }
        bool operator<(const_iterator x) const { return ptr < x.ptr; }
    };

    class const_reverse_iterator {
        const T* ptr;
    public:
        typedef Diff difference_type;
        typedef const T value_type;
        typedef const T* pointer;
        typedef const T& reference;
        typedef std::bidirectional_iterator_tag iterator_category;
        const_reverse_iterator(const T* ptr_) : ptr(ptr_) {}
        const_reverse_iterator(reverse_iterator x) : ptr(&(*x)) {}
        const T& operator*() const { return *ptr; }
        const T* operator->() const { return ptr; }
        const_reverse_iterator& operator--() { ptr++; return *this; }
        const_reverse_iterator& operator++() { ptr--; return *this; }
        const_reverse_iterator operator++(int) { const_reverse_iterator copy(*this); ++(*this); return copy; }
        const_reverse_iterator operator--(int) { const_reverse_iterator copy(*this); --(*this); return copy; }
        bool operator==(const_reverse_iterator x) const { return ptr == x.ptr; }
        bool operator!=(const_reverse_iterator x) const { return ptr != x.ptr; }
    };

private:
#pragma pack(push, 1)
    union direct_or_indirect {
        char direct[sizeof(T) * N];
        struct {
            char* indirect;
            size_type capacity;
        } indirect_contents;
    };
#pragma pack(pop)
    alignas(char*) direct_or_indirect _union = {};
    size_type _size = 0;

    static_assert(alignof(char*) % alignof(size_type) == 0 && sizeof(char*) % alignof(size_type) == 0, "size_type cannot have more restrictive alignment requirement than pointer");
    static_assert(alignof(char*) % alignof(T) == 0, "value_type T cannot have more restrictive alignment requirement than pointer");

    T* direct_ptr(difference_type pos) { return reinterpret_cast<T*>(_union.direct) + pos; }
    const T* direct_ptr(difference_type pos) const { return reinterpret_cast<const T*>(_union.direct) + pos; }
    T* indirect_ptr(difference_type pos) { return reinterpret_cast<T*>(_union.indirect_contents.indirect) + pos; }
    const T* indirect_ptr(difference_type pos) const { return reinterpret_cast<const T*>(_union.indirect_contents.indirect) + pos; }
    bool is_direct() const { return _size <= N; }

    void change_capacity(size_type new_capacity) {
        if (new_capacity <= N) {
            if (!is_direct()) {
                T* indirect = indirect_ptr(0);
                T* src = indirect;
                T* dst = direct_ptr(0);
                memcpy(dst, src, size() * sizeof(T));
                free(indirect);
                _size -= N + 1;
            }
        } else {
            if (!is_direct()) {
                /* FIXME: Because malloc/realloc here won't call new_handler if allocation fails, assert
                    success. These should instead use an allocator or new/delete so that handlers
                    are called as necessary, but performance would be slightly degraded by doing so. */
                _union.indirect_contents.indirect = static_cast<char*>(realloc(_union.indirect_contents.indirect, ((size_t)sizeof(T)) * new_capacity));
                assert(_union.indirect_contents.indirect);
                _union.indirect_contents.capacity = new_capacity;
            } else {
                char* new_indirect = static_cast<char*>(malloc(((size_t)sizeof(T)) * new_capacity));
                assert(new_indirect);
                T* src = direct_ptr(0);
                T* dst = reinterpret_cast<T*>(new_indirect);
                memcpy(dst, src, size() * sizeof(T));
                _union.indirect_contents.indirect = new_indirect;
                _union.indirect_contents.capacity = new_capacity;
                _size += N + 1;
            }
        }
    }

    T* item_ptr(difference_type pos) { return is_direct() ? direct_ptr(pos) : indirect_ptr(pos); }
    const T* item_ptr(difference_type pos) const { return is_direct() ? direct_ptr(pos) : indirect_ptr(pos); }

    void fill(T* dst, ptrdiff_t count, const T& value = T{}) {
        std::fill_n(dst, count, value);
    }

    template<typename InputIterator>
    void fill(T* dst, InputIterator first, InputIterator last) {
        while (first != last) {
            new(static_cast<void*>(dst)) T(*first);
            ++dst;
            ++first;
        }
    }

public:
    void assign(size_type n, const T& val) {
        clear();
        if (capacity() < n) {
            change_capacity(n);
        }
        _size += n;
        fill(item_ptr(0), n, val);
    }

    template<typename InputIterator>
    void assign(InputIterator first, InputIterator last) {
        size_type n = last - first;
        clear();
        if (capacity() < n) {
            change_capacity(n);
        }
        _size += n;
        fill(item_ptr(0), first, last);
    }

    prevector() {}
