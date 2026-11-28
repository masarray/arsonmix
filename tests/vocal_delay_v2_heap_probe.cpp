// SPDX-License-Identifier: AGPL-3.0-only
// Separate translation unit: genuine replaceable global operators, no GCC
// inlined malloc/free false positive; no warning suppression or LTO.
#include <atomic>
#include <cstdlib>
#include <new>
std::atomic<bool> noHeap{false};
void* operator new(std::size_t n){if(noHeap.load())std::abort();if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
void operator delete[](void* p,std::size_t) noexcept{std::free(p);}
