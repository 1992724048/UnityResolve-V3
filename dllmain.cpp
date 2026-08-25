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

                std::stringstream ss;
                for (const auto val : unity::details::unity_assembly | std::views::values) {
                    ss << "assembly: " << val->name() << '\n';
                    for (auto unity_class : val->content()) {
                        ss << "class " << unity_class.lock()->name() << " {\n";
                        for (auto field : unity_class.lock()->content<unity::UnityField>()) {
                            const auto f = field.lock();
                            ss << "\t" << f->type().lock()->name() << ' ' << f->name() << ";\n";
                        }
                        ss << '\n';
                        for (auto method : unity_class.lock()->content<unity::UnityMethod>()) {
                            const auto m = method.lock();
                            ss << "\t" << m->type().lock()->name() << ' ' << m->name() << "(";
                            for (auto& [name, type] : m->args().map()) {
                                ss << type.lock()->name() << " " << name << ", ";
                            }
                            ss << ");\n";
                        }
                        ss << "}\n\n";
                    }
                }
                DLOG << ss.str();
            }).detach();
        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
        case DLL_PROCESS_DETACH:
            break;
        default: ;
    }
    return TRUE;
}
