#ifndef UNORDERED_SET_MT_H
#define UNORDERED_SET_MT_H

#include <unordered_set>
#include <windows.h>
#include <cstddef>

template<typename T>
class unordered_set_mt{
public:
  unordered_set_mt();
  ~unordered_set_mt();
  void insert(const T& elem);
  void erase(const T& elem);
  std::size_t size();

  typename std::unordered_set<T>::const_iterator begin();
  typename std::unordered_set<T>::const_iterator end();

  void lock();
  void unlock();

private:
  std::unordered_set<T> uset;
  HANDLE mutex;
};

template<typename T>
unordered_set_mt<T>::unordered_set_mt(){
  mutex = CreateMutex(NULL, FALSE, NULL);
}

template<typename T>
unordered_set_mt<T>::~unordered_set_mt(){
  CloseHandle(mutex);
}

template<typename T>
void unordered_set_mt<T>::insert(const T& elem){
  lock();
  uset.insert(elem);
  unlock();
}

template<typename T>
void unordered_set_mt<T>::erase(const T& elem){
  lock();
  uset.erase(elem);
  unlock();
}

template<typename T>
std::size_t unordered_set_mt<T>::size(){
  std::size_t ret;
  lock();
  ret = uset.size();
  unlock();
  return ret;
}

template<typename T>
void unordered_set_mt<T>::lock(){
  WaitForSingleObject(mutex, INFINITE);
}

template<typename T>
void unordered_set_mt<T>::unlock(){
  ReleaseMutex(mutex);
}

template<typename T>
typename std::unordered_set<T>::const_iterator unordered_set_mt<T>::begin(){
  return uset.begin();
}

template<typename T>
typename std::unordered_set<T>::const_iterator unordered_set_mt<T>::end(){
  return uset.end();
}

#endif // UNORDERED_SET_MT_H
