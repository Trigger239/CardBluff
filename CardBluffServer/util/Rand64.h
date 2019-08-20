#ifndef RAND64_H
#define RAND64_H

#include "tr1/random"

class Rand64
{
public:

    Rand64();
    unsigned long long generate();
    unsigned long long get_last();

private:

    std::tr1::mersenne_twister<uint_fast64_t,
        64, 312, 156, 31, 0xb5026f5aa96619e9,
        29, 17, 0x71d67fffeda60000,
        37, 0xfff7eee000000000, 43> generator;
    std::tr1::uniform_int<unsigned long long> unif;
    unsigned long long last_value;
};

#endif // RAND64_H
