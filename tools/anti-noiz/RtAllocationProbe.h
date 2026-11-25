// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Standalone Linux qualification only; never linked into the product.
// Wrap direct C allocator references at link time and replace global C++ new/
// delete (including aligned/sized/nothrow variants). This does not intercept
// allocations internal to dynamically linked libraries or OS/kernel calls.
#include <cstdlib>
#include <new>
#include <cstdint>
namespace rtprobe {
struct Counts { std::uint64_t cppAlloc=0,cppFree=0,cAlloc=0,cFree=0; };
inline thread_local bool active=false;
inline thread_local Counts counts{};
inline void begin() noexcept {counts={};active=true;}
inline Counts end() noexcept {active=false;return counts;}
inline bool empty(Counts c) noexcept {return !(c.cppAlloc||c.cppFree||c.cAlloc||c.cFree);}
inline Counts& operator+=(Counts& a,Counts b) noexcept {
    a.cppAlloc+=b.cppAlloc;a.cppFree+=b.cppFree;a.cAlloc+=b.cAlloc;a.cFree+=b.cFree;return a;
}
}
#ifdef ANTI_NOIZ_ALLOC_PROBE
extern "C" {
void* __real_malloc(std::size_t);
void* __real_calloc(std::size_t,std::size_t);
void* __real_realloc(void*,std::size_t);
void __real_free(void*);
void* __real_aligned_alloc(std::size_t,std::size_t);
int __real_posix_memalign(void**,std::size_t,std::size_t);
void* __wrap_malloc(std::size_t n){if(rtprobe::active)++rtprobe::counts.cAlloc;return __real_malloc(n);}
void* __wrap_calloc(std::size_t n,std::size_t s){if(rtprobe::active)++rtprobe::counts.cAlloc;return __real_calloc(n,s);}
void* __wrap_realloc(void* p,std::size_t n){if(rtprobe::active)++rtprobe::counts.cAlloc;return __real_realloc(p,n);}
void __wrap_free(void* p){if(rtprobe::active&&p)++rtprobe::counts.cFree;__real_free(p);}
void* __wrap_aligned_alloc(std::size_t a,std::size_t n){if(rtprobe::active)++rtprobe::counts.cAlloc;return __real_aligned_alloc(a,n);}
int __wrap_posix_memalign(void** p,std::size_t a,std::size_t n){if(rtprobe::active)++rtprobe::counts.cAlloc;return __real_posix_memalign(p,a,n);}
}
namespace rtprobe {
inline void* allocate(std::size_t n,std::size_t alignment=0){
    if(active)++counts.cppAlloc;
    void* p=nullptr;
    if(alignment){if(__real_posix_memalign(&p,alignment,n?n:1)!=0)p=nullptr;}
    else p=__real_malloc(n?n:1);
    if(!p)throw std::bad_alloc();
    return p;
}
inline void release(void* p) noexcept {if(active&&p)++counts.cppFree;__real_free(p);}
}
void* operator new(std::size_t n){return rtprobe::allocate(n);}
void* operator new[](std::size_t n){return rtprobe::allocate(n);}
void* operator new(std::size_t n,std::align_val_t a){return rtprobe::allocate(n,static_cast<std::size_t>(a));}
void* operator new[](std::size_t n,std::align_val_t a){return rtprobe::allocate(n,static_cast<std::size_t>(a));}
void* operator new(std::size_t n,const std::nothrow_t&) noexcept {try{return rtprobe::allocate(n);}catch(...){return nullptr;}}
void* operator new[](std::size_t n,const std::nothrow_t&) noexcept {try{return rtprobe::allocate(n);}catch(...){return nullptr;}}
void* operator new(std::size_t n,std::align_val_t a,const std::nothrow_t&) noexcept {try{return rtprobe::allocate(n,static_cast<std::size_t>(a));}catch(...){return nullptr;}}
void* operator new[](std::size_t n,std::align_val_t a,const std::nothrow_t&) noexcept {try{return rtprobe::allocate(n,static_cast<std::size_t>(a));}catch(...){return nullptr;}}
void operator delete(void* p) noexcept {rtprobe::release(p);}
void operator delete[](void* p) noexcept {rtprobe::release(p);}
void operator delete(void* p,std::size_t) noexcept {rtprobe::release(p);}
void operator delete[](void* p,std::size_t) noexcept {rtprobe::release(p);}
void operator delete(void* p,std::align_val_t) noexcept {rtprobe::release(p);}
void operator delete[](void* p,std::align_val_t) noexcept {rtprobe::release(p);}
void operator delete(void* p,std::size_t,std::align_val_t) noexcept {rtprobe::release(p);}
void operator delete[](void* p,std::size_t,std::align_val_t) noexcept {rtprobe::release(p);}
void operator delete(void* p,const std::nothrow_t&) noexcept {rtprobe::release(p);}
void operator delete[](void* p,const std::nothrow_t&) noexcept {rtprobe::release(p);}
void operator delete(void* p,std::align_val_t,const std::nothrow_t&) noexcept {rtprobe::release(p);}
void operator delete[](void* p,std::align_val_t,const std::nothrow_t&) noexcept {rtprobe::release(p);}
#endif
