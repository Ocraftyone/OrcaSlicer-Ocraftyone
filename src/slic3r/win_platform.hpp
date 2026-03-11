#ifndef ORCASLICER_WIN_PLATFORM_HPP
#define ORCASLICER_WIN_PLATFORM_HPP

#ifdef WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <Windows.h>
    #include <CommCtrl.h>
    #ifdef _MSC_VER
        #include <urlmon.h>
    #endif
#endif

#endif // ORCASLICER_WIN_PLATFORM_HPP
