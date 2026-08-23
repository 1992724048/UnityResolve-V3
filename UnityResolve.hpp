#pragma once
#include <concepts>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <span>
#include <string>
#include <type_traits>

#if defined(GLM_VERSION) || __has_include(<glm/glm.hpp>)
#include <glm/glm.hpp>
#else
#error "Please use the glm math library."
#endif

#if (defined(__i386__) || defined(__x86__)) || (defined(__arm__) && !defined(__aarch64__))
#error "32-bit is not supported, please use a 64-bit compiler."
#endif

#if defined(_WIN32) || defined(_WIN64)
#include <Windows.h>
#define UNITY_CALLING_CONVENTION __fastcall
#define WINDOWS_MODE
#elif defined(__ANDROID__) || defined(__APPLE__) || defined(__MACH__) || defined(__linux__) || defined(__harmony__)
#include <locale>
#include <dlfcn.h>
#define UNITY_CALLING_CONVENTION
#define OTHERS_MODE
#endif

#define UTYPE(type) unity::details::TypeIdentity<type>{}

namespace unity {
    enum class UnityMode : std::uint8_t { IL2CPP, MONO };

    class UnityType;
    class UnityClass;
    class UnityField;
    class UnityMethod;
    class UnityAssembly;
    class UnityMethodArgs;

    namespace util {
        template<typename T>
        struct TypeIdentity {
            using Type = T;
        };

        template<typename T>
        auto try_find(std::shared_mutex& mutex, const std::string_view& name, const std::map<std::string_view, std::shared_ptr<T>>& container) -> std::optional<std::weak_ptr<T>> {
            std::shared_lock _(mutex);
            const auto it = container.find(name);
            if (it == container.end()) {
                return std::nullopt;
            }
            return it->second;
        }
    }

    class UnityAssembly final {
        std::uintptr_t native_ptr{};
        std::string_view native_name;

        mutable std::shared_mutex mutex;
        std::map<std::string_view, std::shared_ptr<UnityClass>> unity_class;
    public:
        UnityAssembly(const std::string_view string, const std::uintptr_t ptr, const std::function<void(std::map<std::string_view, std::shared_ptr<UnityClass>>)>& callback) {
            native_name = string;
            native_ptr = ptr;
            callback(unity_class);
        }

        auto name() const noexcept -> std::string_view {
            return native_name;
        }

        auto ptr() const noexcept -> std::uintptr_t {
            return native_ptr;
        }

        auto operator[](const std::string_view& class_name) const -> std::optional<std::weak_ptr<UnityClass>> {
            return util::try_find(mutex, class_name, unity_class);
        }
    };

    class UnityType final {
        std::uintptr_t native_ptr{};
        std::string_view native_name;
        std::size_t native_size{};
    public:
        auto name() const noexcept -> std::string_view {
            return native_name;
        }

        auto ptr() const noexcept -> std::uintptr_t {
            return native_ptr;
        }

        auto size() const noexcept -> std::size_t {
            return native_size;
        }
    };

    template<typename T> concept ClassMember = std::is_same_v<T, UnityField> || std::is_same_v<T, UnityMethod>;

    class UnityClass final {
        std::uintptr_t native_ptr{};
        std::weak_ptr<UnityAssembly> native_assembly;
        std::weak_ptr<UnityType> native_type;

        std::string_view native_name;
        std::string_view native_parent;
        std::string_view native_namespace;

        mutable std::shared_mutex mutex;
        std::map<std::string_view, std::shared_ptr<UnityField>> unity_filed;
        std::map<std::string_view, std::shared_ptr<UnityMethod>> unity_method;
    public:
        auto name() const noexcept -> std::string_view {
            return native_name;
        }

        auto ptr() const noexcept -> std::uintptr_t {
            return native_ptr;
        }

        auto parent() const noexcept -> std::string_view {
            return native_parent;
        }

        auto name_space() const noexcept -> std::string_view {
            return native_namespace;
        }

        auto assembly() const noexcept -> std::weak_ptr<UnityAssembly> {
            return native_assembly;
        }

        template<ClassMember T>
        auto operator[](util::TypeIdentity<T> /* unused */, const std::string_view name) const -> std::optional<std::weak_ptr<T>> {
            const auto& container = get_container<T>();
            return util::try_find(mutex, name, container);
        }
    private:
        template<ClassMember U>
        const auto& get_container() const {
            if constexpr (std::is_same_v<U, UnityField>) {
                return unity_filed;
            } else {
                return unity_method;
            }
        }
    };

    class UnityField final {
        std::uintptr_t native_ptr{};

        std::weak_ptr<UnityClass> native_class;
        std::weak_ptr<UnityType> native_type;

        std::string_view native_name;
        std::size_t native_offset{};

        bool native_is_static{};
    public:
        auto name() const noexcept -> std::string_view {
            return native_name;
        }

        auto ptr() const noexcept -> std::uintptr_t {
            return native_ptr;
        }

        auto type() const noexcept -> std::weak_ptr<UnityType> {
            return native_type;
        }

        auto offset() const noexcept -> std::size_t {
            return native_offset;
        }

        auto parent() const noexcept -> std::weak_ptr<UnityClass> {
            return native_class;
        }

        auto is_static() const noexcept -> bool {
            return native_is_static;
        }
    };

    class UnityMethodArgs {
        mutable std::shared_mutex mutex;
        std::map<std::string_view, std::shared_ptr<UnityType>> args;
    public:
        auto count() const noexcept -> std::size_t {
            return args.size();
        }

        auto operator[](const std::string_view arg_name) const -> std::optional<std::weak_ptr<UnityType>> {
            return util::try_find(mutex, arg_name, args);
        }
    };

    class UnityMethod final {
        std::uintptr_t native_ptr{};
        std::uintptr_t native_call_ptr{};

        std::weak_ptr<UnityClass> native_class;
        std::weak_ptr<UnityType> native_type;

        std::string_view native_name;
        std::size_t native_offset{};

        UnityMethodArgs native_args;

        bool native_is_static{};
    public:
        auto name() const noexcept -> std::string_view {
            return native_name;
        }

        auto ptr() const noexcept -> std::uintptr_t {
            return native_ptr;
        }

        auto type() const noexcept -> std::weak_ptr<UnityType> {
            return native_type;
        }

        auto args() const noexcept -> const UnityMethodArgs& {
            return native_args;
        }

        auto offset() const noexcept -> std::size_t {
            return native_offset;
        }

        auto parent() const noexcept -> std::weak_ptr<UnityClass> {
            return native_class;
        }

        auto is_static() const noexcept -> bool {
            return native_is_static;
        }

        template<typename Return, typename... Args>
        auto invoke(Args&&... args) -> Return {
            return reinterpret_cast<Return(UNITY_CALLING_CONVENTION*)(Args...)>(native_call_ptr)(std::forward<Args>(args)...);
        }
    };

    namespace details {
        inline UnityMode mode;
        inline void* module_handle;
        inline void* unity_domain;
        inline std::map<std::string_view, std::shared_ptr<UnityAssembly>> unity_assembly;
        inline std::map<std::uintptr_t, std::shared_ptr<UnityType>> unity_types;

        template<typename Return, typename... Args>
        auto invoke_dyn_library(const std::string_view& func_name, Args&&... args) -> std::optional<std::conditional_t<std::is_void_v<Return>, std::monostate, Return>> {
            using OptionalResult = std::optional<std::conditional_t<std::is_void_v<Return>, std::monostate, Return>>;
            using FuncPtr = Return(UNITY_CALLING_CONVENTION*)(std::decay_t<Args>...);
            static std::unordered_map<std::string_view, std::uintptr_t> func_address;

            if (module_handle == nullptr) {
                return std::nullopt;
            }

            auto it = func_address.find(func_name);
            if (it == func_address.end()) {
                std::uintptr_t address = 0;
                #ifdef WINDOWS_MODE
                address = reinterpret_cast<std::uintptr_t>(GetProcAddress(static_cast<HMODULE>(module_handle), func_name.data()));
                #else
                address = reinterpret_cast<std::uintptr_t>(dlsym(module_handle, func_name.data()));
                #endif

                if (address == 0) {
                    return std::nullopt;
                }

                it = func_address.emplace(func_name, address).first;
            }

            auto func = reinterpret_cast<FuncPtr>(it->second);
            try {
                if constexpr (std::is_void_v<Return>) {
                    func(std::forward<Args>(args)...);
                    return OptionalResult{std::monostate{}};
                } else {
                    return OptionalResult{func(std::forward<Args>(args)...)};
                }
            } catch (...) {
                return std::nullopt;
            }
        }

        inline auto auto_select(const std::string_view il2cpp, const std::string_view mono) -> std::string_view {
            if (mode == UnityMode::IL2CPP) {
                return il2cpp;
            }
            return mono;
        }

        inline auto do_thread_attach_detach(const std::string_view il2cpp_name, const std::string_view mono_name, const std::string_view jit_name) -> bool {
            const std::string_view call_name = auto_select(il2cpp_name, mono_name);
            if (mode == UnityMode::IL2CPP) {
                return invoke_dyn_library<void*>(call_name).has_value();
            }
            return invoke_dyn_library<void*>(call_name).has_value() && invoke_dyn_library<void*>(jit_name).has_value();
        }

        inline auto thread_attach() -> bool {
            return do_thread_attach_detach("il2cpp_thread_attach", "mono_thread_attach", "mono_jit_thread_attach");
        }

        inline auto thread_detach() -> bool {
            return do_thread_attach_detach("il2cpp_thread_detach", "mono_thread_detach", "mono_jit_thread_detach");
        }

        inline auto update_domain() -> bool {
            const std::string_view call_name = auto_select("il2cpp_domain_get", "mono_get_root_domain");
            if (const auto result = invoke_dyn_library<decltype(unity_domain)>(call_name)) {
                unity_domain = *result;
                return true;
            }
            return false;
        }
    }

    namespace process {
        namespace impl {
            inline auto do_load_assembly(void* assembly_ptr, const std::string_view assembly_get_image, const std::string_view image_get_name) -> void {
                if (assembly_ptr == nullptr) {
                    return;
                }

                const auto image_opt = details::invoke_dyn_library<void*>(assembly_get_image, assembly_ptr);
                if (!image_opt) {
                    return;
                }

                const auto name_opt = details::invoke_dyn_library<const char*>(image_get_name, *image_opt);
                const auto assembly = std::make_shared<UnityAssembly>(*name_opt, reinterpret_cast<std::uintptr_t>(assembly_ptr), nullptr);
                details::unity_assembly[*name_opt] = std::move(assembly);
            }

            inline auto try_load_assembly_il2cpp() -> void {
                size_t size = 0;
                const auto result = details::invoke_dyn_library<void**>("il2cpp_domain_get_assemblies", details::unity_domain, &size);
                if (!result) {
                    return;
                }

                for (const auto assembly_ptr : std::span{*result, size}) {
                    do_load_assembly(assembly_ptr, "il2cpp_assembly_get_image", "il2cpp_image_get_name");
                }
            }

            inline auto try_load_assembly_mono() -> void {
                auto callback = [](void* assembly_ptr, void* /* unused */) {
                    return do_load_assembly(assembly_ptr, "mono_assembly_get_image", "mono_image_get_name");
                };
                details::invoke_dyn_library<void*, void(*)(void*, void*), void*>("mono_assembly_foreach", callback, nullptr);
            }
        }

        inline auto try_load_assembly() -> void {
            if (details::mode == UnityMode::IL2CPP) {
                impl::try_load_assembly_il2cpp();
            } else {
                impl::try_load_assembly_il2cpp();
            }
        }
    }

    inline auto set_params(const UnityMode unity_mode, void* module_handle) -> void {
        details::mode = unity_mode;
        details::module_handle = module_handle;
    }

    inline auto update() -> bool {
        details::unity_assembly.clear();
        details::unity_types.clear();

        if (!details::update_domain()) {
            return false;
        }
        if (!details::thread_attach()) {
            return false;
        }

        process::try_load_assembly();
        return true;
    }
} // namespace unity
