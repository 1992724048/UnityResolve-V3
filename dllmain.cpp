#include <Windows.h>
#include <ranges>

#include "UnityResolve.hpp"
#include "stdpp/logger.hpp"

auto APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) -> BOOL {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
            std::thread([] {
                stdpp::log::ConsoleManager::open_console();
                set_level(stdpp::log::Level::Trace);
                stdpp::log::add_sink<stdpp::log::ConsoleSink>();

                // GameAssembly.dll
                // mono-2.0-bdwgc.dll
                set_params(unity::UnityMode::IL2CPP, GetModuleHandleW(L"GameAssembly.dll"));
                unity::update();

                for (auto val : unity::details::unity_assembly | std::views::values) {
                    TLOG << val->name();
                    for (auto& unity_class : val->content()) {
                        TLOG << unity_class->name();
                    }
                }
            }).detach();
        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
        case DLL_PROCESS_DETACH:
            break;
        default: ;
    }
    return TRUE;
}
