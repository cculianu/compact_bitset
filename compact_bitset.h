/*
 * compact_bitset - A drop-in replacement for std::bitset that doesn't
 * waste memory.
 *
 * Copyright (c) 2001 Calin A. Culianu <calin.culianu@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
*/
#pragma once
#include <algorithm>
#include <array>
#include <cstddef> // for std::byte
#include <cstdint>
#include <cstring> // for std::memcpy
#include <functional>
#include <istream>
#include <locale>
#include <ostream>
#include <stdexcept>
#include <string>
#include <type_traits>

/// A drop-in replacement for std::bitset that doesn't waste memory if the bitset is small. It tries to use
/// the minimal word size it can for small bitsets, otherwise it defaults to using 64-bit words.
template<std::size_t N,
         // we handle special cases for small sizes <64 bits compactly
         typename T = std::conditional_t<N <= 8, std::uint8_t,
                                         std::conditional_t<N <= 16, std::uint16_t,
                                                            std::conditional_t<N <= 32, std::uint32_t,
                                                                               std::uint64_t>>>>
class compact_bitset
{
    static_assert (std::is_integral_v<T> && std::is_unsigned_v<T> && !std::is_const_v<T> && !std::is_same_v<T, bool> && !std::is_reference_v<T>,
                   "Underlying type for compact_bitset must be an unsigned integral type that is not const or bool");
    static constexpr std::size_t TBits = sizeof(T) * 8;
    static constexpr std::size_t NFullyUsedWords = N / TBits;
    static constexpr std::size_t NBitsRem = N % TBits;
    static constexpr std::size_t NWords = NFullyUsedWords + bool(NBitsRem);
    static constexpr T AllMask = ~T(0);
    static constexpr T LastWordMask = (T(1) << NBitsRem) - 1;
    using DataArray = std::array<T, NWords>;
    DataArray data; // unused bits in this array are always 0
public:
    class reference {
        friend class compact_bitset<N, T>;
        T & word; ///< reference into the data array above
        const std::size_t bpos; ///< bitpos of the bit in question in the word reference above
        constexpr reference(T & w, std::size_t p) noexcept : word(w), bpos(p) {}
    public:
        /// Assign a boolean to the referenced bit
        constexpr reference & operator=(bool b) noexcept { word = b ? word | T{1} << bpos : word & ~(T{1} << bpos); return *this; }
        /// Copy-assign: note that *this still points to the same bit. This simply assigns the value of `o` to *this.
        constexpr reference & operator=(const reference &o) noexcept { return *this = bool(o); }
        /// Implicit conversion to bool for the referenced bit
        constexpr operator bool() const noexcept { return bool(word >> bpos & 0x1); }
        /// Return the inverse of the referenced bit
        constexpr bool operator~() const noexcept { return !bool(*this); }
        /// Flip the referenced bit
        constexpr reference &flip() noexcept { return *this = !bool(*this); }
    };
private:
    constexpr reference make_ref(std::size_t i) noexcept { return reference(data[i / TBits], i % TBits); }
    constexpr const reference make_ref(std::size_t i) const noexcept { return const_cast<compact_bitset &>(*this).make_ref(i); }
    void throw_if_out_of_range(std::size_t pos) const {
        if (pos >= size()) throw std::out_of_range("Out-of-range bit position specified to compact_bitset");
    }
    template <typename Int>
    Int do_int_convert() const noexcept {
        if constexpr (N == 0) return 0;
        Int ret{};
        static constexpr std::size_t IntBits = sizeof(ret) * 8;
        std::size_t bitOffset = 0;
        const auto handle_word = [&bitOffset, &ret](auto word) {
            while (word) {
                std::size_t bit = std::size_t(ffs(word) - 1);
                word &= ~(T{0x1} << bit); // clear bit in word so we can keep looping
                bit += bitOffset; // offset into destination word
                if (bit >= IntBits)
                    return;
                ret |= Int(0x1) << bit; // set bit
            }
        };
        for (std::size_t w = 0; w < NFullyUsedWords && bitOffset < IntBits; ++w, bitOffset += TBits)
            handle_word(data[w]);
        if constexpr (LastWordMask != 0) {
            // handle last 'partial' word in array
            if (bitOffset < IntBits)
                handle_word(data[NWords - 1] & LastWordMask);
        }
        return ret;
    }
    // helper that uses intrinsics to count the number of set bits in a word
    template <typename Int, std::enable_if_t<std::is_integral_v<Int>, int> = 0>
    static int get_popcount(const Int word) {
#if defined(__clang__) || defined(__GNUC__)
        if constexpr (sizeof(Int) <= sizeof(unsigned long long)) {
            using UInt = std::make_unsigned_t<Int>;
            const UInt uword = static_cast<UInt>(word);
            if constexpr (sizeof(uword) <= sizeof(unsigned int))
                return __builtin_popcount(static_cast<unsigned int>(uword));
            else if constexpr (std::is_same_v<UInt, unsigned long>)
                return __builtin_popcountl(uword);
            else // unsigned long long
                return __builtin_popcountll(static_cast<unsigned long long>(uword));
        }
#endif
        // fall back to slow method if no builtin_popcount or if operating on __int128_t
        int ret = 0;
        constexpr std::size_t nBits = sizeof(word) * 8;
        for (std::size_t i = 0; i < nBits; ++i)
            if (word & (Int(0x1) << i)) ++ret;
        return ret;
    }
    // helper that uses intrinsics to get one-plus the index of the first set bit
    template <typename Int, std::enable_if_t<std::is_integral_v<Int>, int> = 0>
    static int ffs(const Int word) {
#if defined(__clang__) || defined(__GNUC__)
        if constexpr (sizeof(Int) <= sizeof(unsigned long long)) {
            using SInt = std::make_signed_t<Int>;
            const SInt sword = static_cast<SInt>(word);
            if constexpr (sizeof(sword) <= sizeof(int))
                return __builtin_ffs(static_cast<int>(sword));
            else if constexpr (std::is_same_v<SInt, long>)
                return __builtin_ffsl(sword);
            else // long long
                return __builtin_ffsll(static_cast<long long>(sword));
        }
#endif
        // fall back to slow method if no builtin_ffs or if operating on __int128_t
        if (word) {
            constexpr int nBits = sizeof(word) * 8;
            for (int i = 0; i < nBits; ++i)
                if (word & (Int(0x1) << i)) return i + 1;
        }
        return 0;
    }
    struct Uninitialized_t {};
    static constexpr Uninitialized_t Uninitialized{};
    constexpr compact_bitset(const Uninitialized_t &) noexcept {} // uninitialized c'tor
public:
    // default-construct: all bits are 0
    constexpr compact_bitset() noexcept : data{{}} {}
    // initialize with bits from val
    constexpr compact_bitset(unsigned long long val) noexcept : compact_bitset() {
        // on gcc & clang the below is hopefully fast as it uses intrinsics
        while (val) {
            const auto bit = std::size_t(ffs(val) - 1); // will be in the range [0, 63]
            if constexpr (sizeof(val) * 8 > N) // this branch is excluded at compile-time for large enough N
                if (bit >= N) break; // val has bits set that we cannot store, break out of loop
            (*this)[bit] = true;
            val &= ~(1ULL << bit); // clear bit
        }
    }
    // initialize with a string e.g. "01101011001"
    template<class CharT, class Traits, class Alloc>
    explicit compact_bitset(const std::basic_string<CharT,Traits,Alloc>& str,
                            typename std::basic_string<CharT,Traits,Alloc>::size_type pos = 0,
                            typename std::basic_string<CharT,Traits,Alloc>::size_type n = std::basic_string<CharT,Traits,Alloc>::npos,
                            CharT zero = CharT('0'),
                            CharT one = CharT('1'))
        : compact_bitset()
    {
        if (pos >= str.size() && N) throw std::out_of_range("Specified string is shorter than pos");
        std::size_t j = 0;
        n = std::min(n, str.size());
        for (auto i = pos; i < n && j < N; ++i, ++j) {
            const auto ch = str[i];
            if (Traits::eq(ch, one)) (*this)[j] = true;
            else if (Traits::eq(ch, zero)) { /* elided -- dest. value already 0 */ }
            else throw std::invalid_argument("Encountered a character in the string that is not 'one' or 'zero'");
        }
    }
    template< class CharT >
    explicit compact_bitset(const CharT* str, typename std::basic_string<CharT>::size_type n = std::basic_string<CharT>::npos,
                            CharT zero = CharT('0'), CharT one = CharT('1'))
        : compact_bitset(n == std::basic_string<CharT>::npos ? std::basic_string<CharT>(str) : std::basic_string<CharT>(str, n), 0, n, zero, one) {}

    // copy-construct
    constexpr compact_bitset(const compact_bitset &o) noexcept { *this = o; }

    // copy-assign
    constexpr compact_bitset &operator=(const compact_bitset &o) noexcept {
        // we do it this way rather than use std::copy in order to support constexpr on pre-C++20
        if constexpr (N > 0) data = o.data;
        return *this;
    }

    constexpr reference operator[](std::size_t pos) noexcept { return make_ref(pos); }
    constexpr bool operator[](std::size_t pos) const noexcept { return make_ref(pos); }
    static constexpr std::size_t size() noexcept { return N; }

    bool test(std::size_t pos) const { throw_if_out_of_range(pos); return (*this)[pos]; }

    /// returns the number of bits set to true
    std::size_t count() const noexcept;
    /// returns true if all bits are true (return true also if size() == 0)
    bool all() const noexcept;
    /// returns true if any of the bits are true
    bool any() const noexcept;
    /// returns true if none of the bits are true
    bool none() const noexcept { return !any(); }

    /// set all bits to true
    compact_bitset & set() noexcept;
    /// set a specific bit -- throws std::out_of_range if pos >= size()
    compact_bitset & set(std::size_t pos, bool value = true) { throw_if_out_of_range(pos); (*this)[pos] = value; return *this; }

    /// sets all bits to false
    compact_bitset & reset() noexcept { return *this = compact_bitset(); }
    /// sets the bit at position pos to false
    compact_bitset & reset(std::size_t pos) { throw_if_out_of_range(pos); (*this)[pos] = false; return *this; }

    /// flips all bits (like operator~, but in-place)
    compact_bitset & flip() noexcept;
    /// flips the bit at position pos -- throws std::out_of_range if pos >= size()
    compact_bitset & flip(std::size_t pos) { throw_if_out_of_range(pos); (*this)[pos].flip(); return *this; }

    /// returns a string representation of the bitset e.g. "00101001101", etc
    template<class CharT = char, class Traits = std::char_traits<CharT>, class Allocator = std::allocator<CharT>>
    std::basic_string<CharT, Traits, Allocator>
    to_string(CharT zero = CharT('0'), CharT one = CharT('1')) const {
        using String = std::basic_string<CharT, Traits, Allocator>;
        using StringSizeT = typename String::size_type;
        String ret(StringSizeT(size()), zero);
        for (std::size_t bit = 0; bit < size(); ++bit)
            if ((*this)[bit]) ret[bit] = one;
        return ret;
    }
    /// Converts the contents of the bitset to an unsigned long integer.
    /// The first bit of the bitset corresponds to the least significant digit of the number and the last bit
    /// corresponds to the most significant digit.
    /// @throws std::overflow_error if the value can not be represented in unsigned long.
    unsigned long to_ulong() const {
        if constexpr (constexpr std::size_t LBits = sizeof(unsigned long) * 8; size() > LBits)
            throw std::overflow_error("This compact_bitset cannot be represented by an unsigned long");
        else
            return do_int_convert<unsigned long>();
    }
    /// Identical to above but returns an unsigned long long.
    unsigned long long to_ullong() const {
        if constexpr (constexpr std::size_t LLBits = sizeof(unsigned long long) * 8; size() > LLBits)
            throw std::overflow_error("This compact_bitset cannot be represented by an unsigned long long");
        else
            return do_int_convert<unsigned long long>();
    }


    // -- bitwise operator support
    friend inline compact_bitset operator&(const compact_bitset & lhs, const compact_bitset & rhs) noexcept {
        compact_bitset ret(compact_bitset::Uninitialized);
        for (std::size_t i = 0; i < ret.size(); ++i)
            ret[i] = lhs[i] & rhs[i];
        if constexpr (ret.LastWordMask != 0) ret.data[ret.NWords-1] &= ret.LastWordMask; // guarantee 0 for unused bits
        return ret;
    }
    friend inline compact_bitset operator|(const compact_bitset & lhs, const compact_bitset & rhs) noexcept {
        compact_bitset ret(compact_bitset::Uninitialized);
        for (std::size_t i = 0; i < ret.size(); ++i)
            ret[i] = lhs[i] | rhs[i];
        if constexpr (ret.LastWordMask != 0) ret.data[ret.NWords-1] &= ret.LastWordMask; // guarantee 0 for unused bits
        return ret;
    }
    friend inline compact_bitset operator^(const compact_bitset & lhs, const compact_bitset & rhs) noexcept {
        compact_bitset ret(compact_bitset::Uninitialized);
        for (std::size_t i = 0; i < ret.size(); ++i)
            ret[i] = lhs[i] ^ rhs[i];
        if constexpr (ret.LastWordMask != 0) ret.data[ret.NWords-1] &= ret.LastWordMask; // guarantee 0 for unused bits
        return ret;
    }
    compact_bitset & operator&=(const compact_bitset & rhs) noexcept { return *this = *this & rhs; }
    compact_bitset & operator|=(const compact_bitset & rhs) noexcept { return *this = *this | rhs; }
    compact_bitset & operator^=(const compact_bitset & rhs) noexcept { return *this = *this ^ rhs; }
    compact_bitset operator~() const noexcept { return compact_bitset{*this}.flip(); }

    // -- bitshift operators
    compact_bitset operator<<(std::size_t shift) const noexcept {
        compact_bitset ret{Uninitialized};
        for (std::size_t i = 0; i < shift && i < N; ++i)
            ret[i] = false;
        for (std::size_t i = 0; i + shift < N; ++i)
            ret[i + shift] = (*this)[i];
        if constexpr (LastWordMask != 0) ret.data[NWords-1] &= LastWordMask; // guarantee 0 for unused bits
        return ret;
    }
    compact_bitset& operator<<=(std::size_t shift) noexcept { return *this = (*this) << shift; }
    compact_bitset operator>>(std::size_t shift) const noexcept {
        compact_bitset ret{Uninitialized};
        const std::size_t endpos = N >= shift ? N - shift : 0;
        // pad left
        for (std::size_t i = endpos; i < N; ++i)
            ret[i] = false;
        for (std::size_t i = 0; i < endpos; ++i)
            ret[i] = (*this)[i + shift];
        if constexpr (LastWordMask != 0) ret.data[NWords-1] &= LastWordMask; // guarantee 0 for unused bits
        return ret;
    }
    compact_bitset& operator>>=(std::size_t shift) noexcept { return *this = (*this) >> shift; }

    bool operator==(const compact_bitset &o) const noexcept;
    bool operator!=(const compact_bitset &o) const noexcept { return !(*this == o); }

    /// std::hash support
    std::size_t hash_code() const noexcept;

    /// access to the underlying data. Note that bits in this array that are unused are guaranteed
    /// to be 0
    const std::byte *bits() const noexcept { return reinterpret_cast<const std::byte *>(data.data()); }
    std::byte *bits() noexcept { return reinterpret_cast<std::byte *>(data.data()); }
    /// returns the number of bytes in the .bits() array
    std::size_t bits_size() const noexcept { return data.size() * sizeof(T); }
};

template <std::size_t N, typename T>
inline
std::size_t compact_bitset<N, T>::count() const noexcept {
    std::size_t ret = 0;
    for (std::size_t i = 0; i < NFullyUsedWords; ++i) ret += get_popcount(data[i]);
    if constexpr (LastWordMask != 0) ret += get_popcount(data[NWords-1] & LastWordMask);
    return ret;
}

template <std::size_t N, typename T>
inline
bool compact_bitset<N, T>::all() const noexcept {
    std::size_t w = 0;
    if constexpr (NFullyUsedWords > 0) {
        for (; w < NFullyUsedWords; ++w)
            if (data[w] != AllMask) return false;
    }
    if constexpr (LastWordMask != 0) {
        if ((data[w] & LastWordMask) != LastWordMask) return false;
    }
    return true;
}

template <std::size_t N, typename T>
inline
bool compact_bitset<N, T>::operator==(const compact_bitset &o) const noexcept {
    std::size_t w = 0;
    if constexpr (NFullyUsedWords > 0) {
        for (; w < NFullyUsedWords; ++w)
            if (data[w] != o.data[w]) return false;
    }
    if constexpr (LastWordMask != 0) {
        if ((data[w] & LastWordMask) != (o.data[w] & LastWordMask)) return false;
    }
    return true;
}

template <std::size_t N, typename T>
inline
bool compact_bitset<N, T>::any() const noexcept {
    std::size_t w = 0;
    if constexpr (NFullyUsedWords > 0) {
        for (; w < NFullyUsedWords; ++w)
            if (data[w]) return true;
    }
    if constexpr (LastWordMask != 0) {
        if (data[w] & LastWordMask) return true;
    }
    return false;
}

template <std::size_t N, typename T>
inline
auto compact_bitset<N, T>::set() noexcept -> compact_bitset & {
    std::size_t w = 0;
    if constexpr (NFullyUsedWords > 0) {
        for (; w < NFullyUsedWords; ++w)
            data[w] = AllMask;
    }
    if constexpr (LastWordMask != 0) {
        data[w] = LastWordMask;
    }
    return *this;
}

template <std::size_t N, typename T>
inline
auto compact_bitset<N, T>::flip() noexcept -> compact_bitset & {
    std::size_t w = 0;
    if constexpr (NFullyUsedWords > 0) {
        for (; w < NFullyUsedWords; ++w)
            data[w] = ~data[w];
    }
    if constexpr (LastWordMask != 0) {
        data[w] = ~data[w] & LastWordMask;
    }
    return *this;
}


/// std::ostream << write support
template <class CharT, class Traits, std::size_t N, typename T>
inline
std::basic_ostream<CharT, Traits>& operator<<(std::basic_ostream<CharT, Traits> & os, const compact_bitset<N, T> & x) {
    const CharT zero = os.widen('0'), one = os.widen('1');
    return os << x.template to_string<CharT, Traits>(zero, one);
}

/// std::istream >> read support. Works exactly like std::bitset.
/// See docs for std::bitset: https://en.cppreference.com/w/cpp/utility/bitset/operator_ltltgtgt2
template <class CharT, class Traits, std::size_t N, typename T>
inline
std::basic_istream<CharT, Traits>& operator>>(std::basic_istream<CharT, Traits> & is, compact_bitset<N, T> & x) {
    const CharT one = is.widen('1'), zero = is.widen('0');
    x.reset();
    for (std::size_t i = 0; i < N && !is.eof(); ++i) {
        if (!is.good())
            break;
        CharT ch = is.peek(); // check
        if (!Traits::eq(ch, one) && !Traits::eq(ch, zero)) {
            if (!i) is.setstate(std::ios_base::failbit); // could not convert even a single character
            break;
        }
        is.get(ch); // consume
        x[i] = Traits::eq(ch, one);
    }
    return is;
}

template<std::size_t N, typename T>
inline
std::size_t compact_bitset<N, T>::hash_code() const noexcept {
    std::size_t ret{};
    const std::byte * const beg = reinterpret_cast<const std::byte *>(data.data());
    const std::byte * const end = beg + data.size() * sizeof(T);
    for (const std::byte *cur = beg; cur < end; cur += sizeof(std::size_t)) {
        std::size_t tmp{};
        std::memcpy(reinterpret_cast<std::byte *>(&tmp), cur, std::min<std::size_t>(end - cur, sizeof(tmp)));
        ret ^= tmp;
    }
    return ret;
}

/// specialization for std::hash
template <std::size_t N, typename T>
struct std::hash<compact_bitset<N, T>> {
    std::size_t operator()(const compact_bitset<N, T> & x) const noexcept { return x.hash_code(); }
};
