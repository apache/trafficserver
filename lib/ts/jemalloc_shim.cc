
#ifdef JEMALLOC_VERSION
extern void* malloc(size_t n) noexcept __attribute__((weak));
extern void free(void *p) noexcept __attribute__((weak));
extern void* operator new(size_t n) __attribute__((weak));
extern void* operator new<>(size_t n) __attribute__((weak));
extern void operator delete(void* p) noexcept __attribute__((weak));
extern void operator delete<>(void* p) noexcept __attribute__((weak));
extern void operator delete(void* p, size_t n) noexcept __attribute__((weak));
extern void operator delete<>(void* p, size_t n) noexcept __attribute__((weak));

void* malloc(size_t n) throw() { return ( n ? mallocx(n,MALLOCX_ZERO) : NULL ); }
void free(void *p) throw() { ( p ? dallocx(p,0) : (void)0 ); }
void* operator new(size_t n) { return mallocx(n,MALLOCX_ZERO); }
void* operator new<>(size_t n) { return mallocx(n,MALLOCX_ZERO); }
void operator delete(void* p) noexcept { return dallocx(p,0); }
void operator delete<>(void* p) noexcept { return dallocx(p,0); }
void operator delete(void* p, size_t n) noexcept { return sdallocx(p,n,0); }
void operator delete<>(void* p, size_t n) noexcept { return sdallocx(p,n,0); }
#endif
