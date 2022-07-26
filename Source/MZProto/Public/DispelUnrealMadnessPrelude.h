#if PLATFORM_WINDOWS

#pragma warning (push)

// forcing value to bool true or false
#pragma warning (disable : 4800)
// decimal digit terminates octal escape sequence
#pragma warning (disable : 4125)
// behavior change __is_pod has different value in previous version
#pragma warning (disable : 4647)
// 'symbol' is not defined as a preprocessor macro,
// replacing with '0' for 'directives'
#pragma warning (disable : 4668)
// constructor is not implicitly called
#pragma warning (disable : 4582)
// destructor is not implicitly called
#pragma warning (disable : 4583)
// reinterpret_cast
#pragma warning (disable : 4946)
#pragma warning (disable : 4459)

// Reader beware: this should only be done if the
// compile platform AND the build target are Windows.

void MemoryBarrier();
#include "Windows/AllowWindowsPlatformTypes.h"
#pragma intrinsic(_InterlockedCompareExchange64)
//#define InterlockedCompareExchangeAcquire64 _InterlockedCompareExchange64
//#define InterlockedCompareExchangeRelease64 _InterlockedCompareExchange64
//#define InterlockedCompareExchangeNoFence64 _InterlockedCompareExchange64
#define InterlockedCompareExchange64 _InterlockedCompareExchange64

#endif 
