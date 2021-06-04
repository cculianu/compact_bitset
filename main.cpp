#include "compact_bitset.h"

#include <iostream>
#include <sstream>

template <std::size_t N>
void test()
{
    std::cout << std::string(80, '-') << "\n";
    compact_bitset<N> cbs, cbs2;
    std::cout << "N: " << N << " sizeof: " << sizeof(cbs) << "\n";
    if constexpr (N > 0) cbs.set(10 % N, true);
    if constexpr (N > 0) cbs2[10 % N] = true;
    std::cout << cbs << "\n";
    std::cout << cbs2 << "\n";
    std::cout << (cbs == cbs2) << "\n";
    if (N) cbs2[0] = true;
    std::cout << cbs << "\n";
    std::cout << cbs2 << "\n";
    std::cout << (cbs == cbs2) << "\n";
    cbs ^= cbs2;
    cbs2 ^= cbs;
    cbs ^= cbs2;
    std::cout << cbs << "\n";
    std::cout << cbs2 << "\n";
    std::cout << (cbs == cbs2) << "\n";
    if (N) cbs[cbs.size()-1] = true;
    std::cout << cbs << "\n";
    std::cout << cbs2 << "\n";
    std::cout << (cbs == cbs2) << "\n";
    std::cout << cbs << ", hash_code: " << cbs.hash_code() << "\n";
    std::cout << cbs2 << ", hash_code: " << cbs2.hash_code() << "\n";
    if (N) cbs2[0] = true;
    if (N) cbs2[cbs2.size()-1] = true;
    std::cout << (cbs == cbs2) << "\n";
    std::cout << ~cbs << " (~) \n";
    std::cout << (cbs << 2) << " (<< 2)\n";
    std::cout << (cbs >> 2) << " (>> 2)\n";
    try {
        std::cout << cbs.to_ulong() << " (to_ulong)\n";
    } catch (const std::overflow_error &e) {
        std::cout << e.what() << "\n";
    }
    try {
        std::cout << cbs.to_ullong() << " (to_ullong)\n";
    } catch (const std::overflow_error &e) {
        std::cout << e.what() << "\n";
    }
    if (N) {
        const auto str = cbs.to_string();
        std::istringstream is(str);
        compact_bitset<N> parsed(str);
        if (parsed != cbs) throw std::runtime_error("Parsed value not equal");
        parsed.reset();
        if (!parsed.none() || parsed.any()) throw std::runtime_error("Expected reset to clear all bits!");
        is >> parsed;
        if (parsed != cbs) throw std::runtime_error("Parsed value not equal (2)");
    }
}

int main()
{
    test<11>();
    test<16>();
    test<32>();
    test<33>();
    test<64>();
    test<73>();
    test<100>();
    test<0>();
    test<1>();
    std::cout << std::string(80, '-') << "\n";
    {
        const std::string s = "01010100110";
        compact_bitset<20> cbs(s);
        std::cout << "Parse: s: " << s << " -> " << cbs.to_string() << "\n";
    }
    std::cout << std::string(80, '-') << "\n";
    {
        const std::string s = "01010100110";
        std::istringstream is(s);
        compact_bitset<20> cbs;
        is >> cbs;
        std::cout << "StramParse: s: " << s << " -> " << cbs.to_string() << "\n";
    }
    return 0;
}
