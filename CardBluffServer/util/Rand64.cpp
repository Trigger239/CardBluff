#include "Rand64.h"
#include <ctime>

Rand64::Rand64()
  :unif(1, 0xFFFFFFFFFFFFFFFF)
  {
    generator.seed((unsigned long long) time(NULL));
    generate();
  }

unsigned long long Rand64::generate(){
  last_value = unif(generator);
  return last_value;
}

unsigned long long Rand64::get_last(){
  return last_value;
}
