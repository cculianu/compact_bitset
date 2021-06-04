#include "compact_bitset.h"

#include <iostream>

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
    if (N) cbs2[0] = true;
    if (N) cbs2[cbs2.size()-1] = true;
    std::cout << cbs << "\n";
    std::cout << cbs2 << "\n";
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
    return 0;
}
