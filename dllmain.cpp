#include <Windows.h>
#include <ranges>

#include "UnityResolve.hpp"
#include "stdpp/logger.hpp"

UNITY_ACCESS;
UNITY_UDL;

namespace {
    class Player : public Class {
    public:
        inline static Field<unity::api::csharp::String*> name;
        inline static Field<int> hp;
        inline static Method<void, Player*, int> set_hp;
        inline static Method<void, int> set_hp_static;
        inline static Property<int, Player> hp_access;
    };

    auto use() -> void {
        const auto class_result = "Test.dll:Player"_class;
        const auto& class_ = *class_result->lock();

        const auto& name_filed = "Test.dll:Player:name"_field;
        const auto& hp_field = *class_[UTYPE(unity::UnityField), "hp"]->lock();

        constexpr auto method = UTYPE(unity::UnityMethod);

        auto& hp_set_field = *class_[method, "set_hp"]->lock();
        auto& hp_static_field = *class_[method, "set_hp_static"]->lock();
        auto& hp_field_set = *class_[method, "hp_access_set"]->lock();
        auto& hp_field_get = *class_[method, "hp_access_get"]->lock();

        Player::hp[hp_field.offset()];
        Player::set_hp[hp_set_field.call()];
        Player::set_hp_static[hp_static_field.call()];
        Player::hp_access[hp_field_get.call(), hp_field_set.call()];

        Player player;
        player[Player::hp] = 114514;
        player[Player::set_hp](114514);
        Player::set_hp_static(114514);
        player[Player::hp_access] = 114514;
        [[maybe_unused]] int hp = player[Player::hp_access];
    }
} // namespace

auto APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) -> BOOL {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
            std::thread([] -> void {
                stdpp::log::ConsoleManager::open_console();
                set_level(stdpp::log::Level::Trace);
                stdpp::log::add_sink<stdpp::log::ConsoleSink>();

                // GameAssembly.dll
                // mono-2.0-bdwgc.dll
                set_params(unity::UnityMode::IL2CPP, GetModuleHandleW(L"GameAssembly.dll"));
                unity::update();

                std::stringstream ss;
                for (const auto& val : unity::details::unity_assembly | std::views::values) {
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
                            for (const auto& [type, name] : m->args().params()) {
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
