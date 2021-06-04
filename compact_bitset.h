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
        std::size_t bpos; ///< bitpos of the bit in question in the word reference above
        constexpr reference(T & w, std::size_t p) noexcept : word(w), bpos(p) {}
    public:
        /// Assign a boolean to the referenced bit
        constexpr reference & operator=(bool b) noexcept { word = b ? word | T{1} << bpos : word & ~(T{1} << bpos); return *this; }
        /// Copy-assign: note that *this still points to the same bit. This simply assigns the value of `o` to *this.
        constexpr reference & operator=(const reference &o) noexcept { return *this = bool(o); }
        /// Implicit conversion to bool for the referenced bit
        constexpr operator bool() const noexcept { return bool(word >> bpos & 0x1); }
        /// Return the inverse of the referenced bit
        constexpr bool operator~() const noexcept { return ~bool(*this); }
        /// Flip the referenced bit
        constexpr reference &flip() noexcept { return *this = !bool(*this); }
    };
private:
    constexpr reference make_ref(std::size_t i) noexcept { return reference(data[i / TBits], i % TBits); }
    constexpr const reference make_ref(std::size_t i) const noexcept { return const_cast<compact_bitset &>(*this).make_ref(i); }
    void throwIfOutOfRange(std::size_t pos) const {
        if (pos >= size()) throw std::out_of_range("Out-of-range bit position specified to compact_bitset");
    }
    template <typename Int>
    Int doIntConvert() const noexcept {
        if constexpr (N == 0) return 0;
        Int ret{};
        constexpr std::size_t IntBytes = sizeof(ret);
        constexpr std::size_t IntBits = IntBytes * 8;
        // TODO: optomize; this may be slower than doing some word-at-a-time reading
        for (std::size_t i = 0; i < IntBits && i < N; ++i)
            ret |= T(bool((*this)[i])) << i;
        return ret;
    }
    struct Uninitialized_t {};
    static constexpr Uninitialized_t Uninitialized{};
    constexpr compact_bitset(const Uninitialized_t &) noexcept {} // uninitialized c'tor
public:
    // default-construct: all bits are 0
    constexpr compact_bitset() noexcept : data{{T(0)}} {}
    // initialize with bits from val
    constexpr compact_bitset(unsigned long long val) noexcept : compact_bitset() {
        // TODO: optomize this -- this may be slower than we would like.
        for (std::size_t i = 0; val && i < N; ++i, val >>= 1)
            if (val & 0x1) (*this)[i] = true;
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
        if (pos >= str.size()) throw std::out_of_range("Specified string is shorter than pos");
        std::size_t j = 0;
        n = std::min(n, str.size());
        for (auto i = pos; i < n && j < N; ++i, ++j) {
            const auto ch = str[i];
            if (Traits::eq(ch, one)) (*this)[j] = true;
            else if (Traits::eq(ch, zero)) (*this)[j] = false;
            else throw std::invalid_argument("Encountere a character in the string that is not 'one' or 'zero'");
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

    bool test(std::size_t pos) const { throwIfOutOfRange(pos); return (*this)[pos]; }

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
    compact_bitset & set(std::size_t pos, bool value = true) { throwIfOutOfRange(pos); (*this)[pos] = value; return *this; }

    /// sets all bits to false
    compact_bitset & reset() noexcept { return *this = compact_bitset(); }
    /// sets the bit at position pos to false
    compact_bitset & reset(std::size_t pos) { throwIfOutOfRange(pos); (*this)[pos] = false; return *this; }

    /// flips all bits (like operator~, but in-place)
    compact_bitset & flip() noexcept;
    /// flips the bit at position pos -- throws std::out_of_range if pos >= size()
    compact_bitset & flip(std::size_t pos) { throwIfOutOfRange(pos); (*this)[pos].flip(); return *this; }

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
            return doIntConvert<unsigned long>();
    }
    /// Identical to above but returns an unsigned long long.
    unsigned long long to_ullong() const {
        if constexpr (constexpr std::size_t LLBits = sizeof(unsigned long long) * 8; size() > LLBits)
            throw std::overflow_error("This compact_bitset cannot be represented by an unsigned long long");
        else
            return doIntConvert<unsigned long long>();
    }


    // -- bitwise operator support
    template<std::size_t Np, typename Tp>
    friend compact_bitset<Np, Tp> operator&(const compact_bitset<Np, Tp> & lhs, const compact_bitset<Np, Tp> & rhs) noexcept {
        compact_bitset<Np, Tp> ret(compact_bitset::Uninitialized);
        for (std::size_t i = 0; i < Np; ++i)
            ret[i] = lhs[i] & rhs[i];
        return ret;
    }
    template<std::size_t Np, typename Tp>
    friend compact_bitset<Np, Tp> operator|(const compact_bitset<Np, Tp> & lhs, const compact_bitset<Np, Tp> & rhs) noexcept {
        compact_bitset<Np, Tp> ret(compact_bitset::Uninitialized);
        for (std::size_t i = 0; i < Np; ++i)
            ret[i] = lhs[i] | rhs[i];
        return ret;
    }
    template<std::size_t Np, typename Tp>
    friend compact_bitset<Np, Tp> operator^(const compact_bitset<Np, Tp> & lhs, const compact_bitset<Np, Tp> & rhs) noexcept {
        compact_bitset<Np, Tp> ret(compact_bitset::Uninitialized);
        for (std::size_t i = 0; i < Np; ++i)
            ret[i] = lhs[i] ^ rhs[i];
        return ret;
    }
    compact_bitset & operator&=(const compact_bitset & rhs) noexcept { return *this = *this & rhs; }
    compact_bitset & operator|=(const compact_bitset & rhs) noexcept { return *this = *this | rhs; }
    compact_bitset & operator^=(const compact_bitset & rhs) noexcept { return *this = *this ^ rhs; }
    constexpr compact_bitset operator~() const noexcept {
        compact_bitset ret{Uninitialized};
        for (std::size_t i = 0; i < N; ++i)
            ret[i] = !(*this)[i];
        return ret;
    }

    // -- bitshift operators
    compact_bitset operator<<(std::size_t shift) const noexcept {
        compact_bitset ret{Uninitialized};
        for (std::size_t i = 0; i < shift && i < N; ++i)
            ret[i] = false;
        for (std::size_t i = 0; i + shift < N; ++i)
            ret[i + shift] = (*this)[i];
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
        return ret;
    }
    compact_bitset& operator>>=(std::size_t shift) noexcept { return *this = (*this) >> shift; }

    bool operator==(const compact_bitset &o) const noexcept;
    bool operator!=(const compact_bitset &o) const noexcept { return !(*this == o); }
};

template <std::size_t N, typename T>
std::size_t compact_bitset<N, T>::count() const noexcept {
    std::size_t ret{};
    // TODO: optimize this to perhaps use intrinsics on some platforms -- this may be slow on some compilers that
    // fail to optimize the below loop properly
    for (std::size_t i = 0; i < size(); ++i) ret += bool((*this)[i]);
    return ret;
}

template <std::size_t N, typename T>
bool compact_bitset<N, T>::all() const noexcept {
    // kind of optimized but really we should be using intrinsics here
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
bool compact_bitset<N, T>::operator==(const compact_bitset &o) const noexcept {
    // kind of optimized but really we should be using intrinsics here
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
bool compact_bitset<N, T>::any() const noexcept {
    // kind of optimized but really we should be using intrinsics here
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
auto compact_bitset<N, T>::set() noexcept -> compact_bitset & {
    // kind of optimized but really we should be using intrinsics here
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
auto compact_bitset<N, T>::flip() noexcept -> compact_bitset & {
    // kind of optimized but really we should be using intrinsics here
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
std::basic_ostream<CharT, Traits>& operator<<(std::basic_ostream<CharT, Traits> & os, const compact_bitset<N, T> & x) {
    const CharT zero = os.widen('0'), one = os.widen('1');
    return os << x.template to_string<CharT, Traits>(zero, one);
}

/// std::istream >> read support. Works exactly like std::bitset.
/// See docs for std::bitset: https://en.cppreference.com/w/cpp/utility/bitset/operator_ltltgtgt2
template <class CharT, class Traits, std::size_t N, typename T>
std::basic_istream<CharT, Traits>& operator>>(std::basic_istream<CharT, Traits> & is, compact_bitset<N, T> & x) {
    const CharT one = is.widen('1'), zero = is.widen('0');
    std::size_t i = 0, ok = 0;
    x.reset();
    for (; i < N && !is.eof(); ++i) {
        if (!is.good())
            break;
        CharT ch = is.peek(); // check
        if (!Traits::eq(ch, one) && !Traits::eq(ch, zero))
            break;
        is.get(ch); // consume
        x[i] = Traits::eq(ch, one);
        ++ok;
    }
    if (N && !ok)
        is.setstate(std::ios_base::failbit);
    return is;
}

/// specialization for std::hash
template <std::size_t N, typename T>
struct std::hash<compact_bitset<N, T>> {
    std::size_t operator()(const compact_bitset<N, T> & x) const {
        if constexpr (N <= sizeof(unsigned long))
            return x.to_ulong();
        else if constexpr (N <= sizeof(unsigned long long))
            return x.to_ullong();
        else
            // otherwise rely on stringifying this and then returning the hash of that
            // TODO: optimize this -- this may be slower than we want.
            return std::hash<std::string>{}(x.to_string());
    }
};

#include <bitset>
