compact_bitset.h
----------------

This is a drop-in relacement for std::bitset, but unlike std::bitset, it doesn't
waste memory.  It tries to use the minimal word size for the bitset in question.

So a bitset of 20 bits would use 32-bit.  A bitset of 12 would use 16, a bitset 
of 5 would use 8 bits, etc.

The maximum word size is 64, but, like std::bitset, it can support arbitary 
bit sets.

The implementation is just a single-header include, compact_bitset.h.

main.cpp for this project is just a bunch of tests, and can be safely ignored.

