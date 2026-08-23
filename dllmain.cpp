#include <Windows.h>
#include "UnityResolve.hpp"
#include "stdpp/logger.hpp"

auto APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) -> BOOL {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
            set_level(stdpp::log::Level::Trace);
            stdpp::log::add_sink<stdpp::log::ConsoleSink>();

            set_params(unity::UnityMode::MONO, GetModuleHandleW(L"mono-2.0-bdwgc.dll"));
            unity::update();

            CMSG << unity::details::unity_assembly.size();
        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
        case DLL_PROCESS_DETACH:
            break;
        default: ;
    }
    return TRUE;
}
