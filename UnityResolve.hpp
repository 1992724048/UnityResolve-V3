#pragma once
#include <chrono>
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
    using namespace std::chrono_literals;

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
    } // namespace util

    class UnityAssembly final {
        std::uintptr_t native_ptr{};
        std::string_view native_name;

        mutable std::shared_mutex mutex;
        std::map<std::string_view, std::shared_ptr<UnityClass>> unity_class;
    public:
        UnityAssembly(const std::string_view name, const std::uintptr_t ptr, const std::function<void(std::map<std::string_view, std::shared_ptr<UnityClass>>&)>& callback) :
            native_ptr{ptr},
            native_name{name} {
            callback(unity_class);
        }

        auto name() const noexcept -> std::string_view {
            return native_name;
        }

        auto ptr() const noexcept -> std::uintptr_t {
            return native_ptr;
        }

        auto content() const noexcept -> auto {
            return unity_class | std::views::values | std::views::transform([](std::shared_ptr<UnityClass> ptr)-> std::weak_ptr<UnityClass> {
                return ptr;
            });
        }

        auto operator[](const std::string_view& class_name) const -> std::optional<std::weak_ptr<UnityClass>> {
            return util::try_find(mutex, class_name, unity_class);
        }
    };

    class UnityType final {
        std::uintptr_t native_ptr{};
        std::uintptr_t native_object{};
        std::string native_name;
        std::size_t native_size{};
    public:
        UnityType(const std::uintptr_t native_ptr, const std::uintptr_t native_object, std::string native_name, const std::size_t native_size) :
            native_ptr{native_ptr},
            native_object{native_object},
            native_name{std::move(native_name)},
            native_size{native_size} {}

        [[nodiscard]] auto name() const noexcept -> const std::string& {
            return native_name;
        }

        [[nodiscard]] auto ptr() const noexcept -> std::uintptr_t {
            return native_ptr;
        }

        [[nodiscard]] auto size() const noexcept -> std::size_t {
            return native_size;
        }

        [[nodiscard]] auto object() const noexcept -> std::uintptr_t {
            return native_object;
        }
    };

    namespace process {
        auto fix_class_depend() -> void;
        auto fix_type_depend() -> void;
    } // namespace process

    template<typename T> concept ClassMember = std::is_same_v<T, UnityField> || std::is_same_v<T, UnityMethod>;

    class UnityClass final {
        std::uintptr_t native_ptr{};
        std::uintptr_t native_parent_ptr{};
        std::uintptr_t native_type_ptr{};
        std::weak_ptr<UnityAssembly> native_assembly;
        std::weak_ptr<UnityType> native_type;
        std::weak_ptr<UnityClass> native_parent;

        std::string_view native_name;
        std::string_view native_namespace;

        mutable std::shared_mutex mutex;
        std::map<std::string_view, std::shared_ptr<UnityField>> unity_filed;
        std::map<std::string_view, std::shared_ptr<UnityMethod>> unity_method;

        friend auto process::fix_class_depend() -> void;
        friend auto process::fix_type_depend() -> void;
    public:
        UnityClass(const std::uintptr_t native_ptr,
                   const std::weak_ptr<UnityAssembly>& native_assembly,
                   const std::weak_ptr<UnityType>& native_type,
                   const std::string_view native_name,
                   const std::uintptr_t native_parent_ptr,
                   const std::string_view native_namespace,
                   const std::function<void(std::map<std::string_view, std::shared_ptr<UnityField>>&)>& field_callback,
                   const std::function<void(std::map<std::string_view, std::shared_ptr<UnityMethod>>&)>& method_callback) :
            native_ptr{native_ptr},
            native_parent_ptr{native_parent_ptr},
            native_assembly{native_assembly},
            native_type{native_type},
            native_name{native_name},
            native_namespace{native_namespace} {
            field_callback(unity_filed);
            method_callback(unity_method);
        }

        auto name() const noexcept -> std::string_view {
            return native_name;
        }

        auto ptr() const noexcept -> std::uintptr_t {
            return native_ptr;
        }

        auto parent() const noexcept -> std::weak_ptr<UnityClass> {
            return native_parent;
        }

        auto name_space() const noexcept -> std::string_view {
            return native_namespace;
        }

        auto assembly() const noexcept -> std::weak_ptr<UnityAssembly> {
            return native_assembly;
        }

        template<ClassMember T>
        auto count() const -> std::size_t {
            const auto& container = get_container<T>();
            return container.size();
        }

        template<ClassMember T>
        auto content() const -> auto {
            return get_container<T>() | std::views::values | std::views::transform([](std::shared_ptr<T> ptr)-> std::weak_ptr<T> {
                return ptr;
            });
        }

        template<ClassMember T>
        auto operator[](util::TypeIdentity<T> /* unused */, const std::string_view name) const -> std::optional<std::weak_ptr<T>> {
            const auto& container = get_container<T>();
            return util::try_find(mutex, name, container);
        }
    private:
        template<ClassMember U>
        auto get_container() const -> const auto& {
            if constexpr (std::is_same_v<U, UnityField>) {
                return unity_filed;
            } else {
                return unity_method;
            }
        }
    };

    class UnityField final {
        std::uintptr_t native_ptr{};
        std::uintptr_t native_class_ptr{};
        std::uintptr_t native_type_ptr{};

        std::weak_ptr<UnityClass> native_class;
        std::weak_ptr<UnityType> native_type;

        std::string_view native_name;
        std::size_t native_offset{};

        bool native_is_static{};

        friend auto process::fix_class_depend() -> void;
        friend auto process::fix_type_depend() -> void;
    public:
        UnityField(const std::uintptr_t native_ptr,
                   const std::uintptr_t native_class_ptr,
                   const std::uintptr_t native_type_ptr,
                   const std::weak_ptr<UnityType>& native_type,
                   const std::string_view& native_name,
                   const std::size_t native_offset) :
            native_ptr{native_ptr},
            native_class_ptr{native_class_ptr},
            native_type_ptr{native_type_ptr},
            native_type{native_type},
            native_name{native_name},
            native_offset{native_offset} {
            native_is_static = native_offset < 0;
        }

        [[nodiscard]] auto name() const noexcept -> std::string_view {
            return native_name;
        }

        [[nodiscard]] auto ptr() const noexcept -> std::uintptr_t {
            return native_ptr;
        }

        [[nodiscard]] auto type() const noexcept -> std::weak_ptr<UnityType> {
            return native_type;
        }

        [[nodiscard]] auto offset() const noexcept -> std::size_t {
            return native_offset;
        }

        [[nodiscard]] auto parent() const noexcept -> std::weak_ptr<UnityClass> {
            return native_class;
        }

        [[nodiscard]] auto is_static() const noexcept -> bool {
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
        std::uintptr_t native_type_ptr{};

        std::weak_ptr<UnityClass> native_class;
        std::weak_ptr<UnityType> native_type;

        std::string_view native_name;
        std::size_t native_offset{};

        UnityMethodArgs native_args;

        bool native_is_static{};

        friend auto process::fix_type_depend() -> void;
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
            // TODO: use 'std::start_lifetime_as' in c++23 if compiler support
            return std::bit_cast<Return(UNITY_CALLING_CONVENTION*)(Args...)>(native_call_ptr)(std::forward<Args>(args)...);
        }
    };

    namespace details {
        inline UnityMode mode;
        inline void* module_handle;
        inline void* unity_domain;
        inline std::map<std::string_view, std::shared_ptr<UnityAssembly>> unity_assembly;
        inline std::map<std::uintptr_t, std::shared_ptr<UnityType>> unity_types;
        inline std::map<std::uintptr_t, std::weak_ptr<UnityClass>> unity_classes;

        template<typename Return, typename... Args>
        auto invoke_dyn_library(const std::string_view& func_name, Args&&... args) -> std::optional<std::conditional_t<std::is_void_v<Return>, std::monostate, Return>> {
            using OptionalResult = std::optional<std::conditional_t<std::is_void_v<Return>, std::monostate, Return>>;
            using FuncPtr = Return(UNITY_CALLING_CONVENTION*)(std::decay_t<Args>...);
            static std::unordered_map<std::string_view, std::uintptr_t> func_address;

            if (module_handle == nullptr) {
                return std::nullopt;
            }

            auto iterator = func_address.find(func_name);
            if (iterator == func_address.end()) {
                std::uintptr_t address = 0;
                #ifdef WINDOWS_MODE
                address = std::bit_cast<std::uintptr_t>(GetProcAddress(static_cast<HMODULE>(module_handle), func_name.data()));
                #else
                address = std::bit_cast<std::uintptr_t>(dlsym(module_handle, func_name.data()));
                #endif

                if (address == 0) {
                    return std::nullopt;
                }

                iterator = func_address.emplace(func_name, address).first;
            }

            auto func = std::bit_cast<FuncPtr>(iterator->second);
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
                while (!invoke_dyn_library<bool>("il2cpp_is_vm_thread", nullptr)) {
                    std::this_thread::sleep_for(100ms);
                }
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
            if (const auto result = invoke_dyn_library<void*>(call_name)) {
                unity_domain = *result;
                return true;
            }
            return false;
        }
    } // namespace details

    namespace process {
        namespace impl {
            inline auto try_find_type(void* type_ptr) -> std::shared_ptr<UnityType>& {
                auto ptr = std::bit_cast<std::uintptr_t>(type_ptr);
                const auto iterator = details::unity_types.find(ptr);
                if (iterator != details::unity_types.end()) {
                    return iterator->second;
                }

                const auto type_name = details::invoke_dyn_library<char*>(details::mode == UnityMode::IL2CPP ? "il2cpp_type_get_name" : "mono_type_get_name", type_ptr);
                const auto type_object = details::invoke_dyn_library<char*>(details::mode == UnityMode::IL2CPP ? "il2cpp_type_get_object" : "mono_type_get_object", type_ptr);
                const auto type = std::make_shared<UnityType>(ptr, reinterpret_cast<std::uintptr_t>(*type_object), *type_name, 0);
                if (details::mode == UnityMode::IL2CPP) {
                    details::invoke_dyn_library<void>("il2cpp_free", *type_name);
                }
                return details::unity_types[ptr] = type;
            }

            inline auto try_load_method_il2cpp() -> void {}
            inline auto try_load_method_mono() -> void {}

            inline auto try_load_method(void* class_ptr, std::map<std::string_view, std::shared_ptr<UnityMethod>>& container) -> void {}

            inline auto foreach_load_field(const std::string_view class_get_fields,
                                           const std::string_view field_get_name,
                                           const std::string_view field_get_type,
                                           const std::string_view field_get_offset,
                                           void* class_ptr,
                                           std::map<std::string_view, std::shared_ptr<UnityField>>& container) -> void {
                void* iter{nullptr};
                void* field_ptr{nullptr};
                do {
                    const auto field_ptr_result = details::invoke_dyn_library<void*>(class_get_fields, class_ptr, &iter);
                    if (!field_ptr_result) {
                        continue;
                    }
                    field_ptr = *field_ptr_result;
                    if (field_ptr == nullptr) {
                        continue;
                    }
                    const auto field_name = details::invoke_dyn_library<const char*>(field_get_name, field_ptr);
                    const auto field_type = details::invoke_dyn_library<void*>(field_get_type, field_ptr);
                    const auto field_offset = details::invoke_dyn_library<int>(field_get_offset, field_ptr);
                    auto type = try_find_type(*field_type);
                    const auto field = std::make_shared<UnityField>(std::bit_cast<std::uintptr_t>(field_ptr),
                                                                    std::bit_cast<std::uintptr_t>(class_ptr),
                                                                    std::bit_cast<std::uintptr_t>(*field_type),
                                                                    type,
                                                                    *field_name,
                                                                    *field_offset);
                    container[*field_name] = field;
                } while (field_ptr != nullptr);
            }

            inline auto try_load_field(void* class_ptr, std::map<std::string_view, std::shared_ptr<UnityField>>& container) -> void {
                if (details::mode == UnityMode::IL2CPP) {
                    foreach_load_field("il2cpp_class_get_fields", "il2cpp_field_get_name", "il2cpp_field_get_type", "il2cpp_field_get_offset", class_ptr, container);
                } else {
                    foreach_load_field("mono_class_get_fields", "mono_field_get_name", "mono_field_get_type", "mono_field_get_offset", class_ptr, container);
                }
            }

            inline auto do_load_class(const std::shared_ptr<UnityAssembly>& assembly,
                                      const std::string_view class_get_name,
                                      const std::string_view class_get_parent,
                                      const std::string_view class_get_namespace,
                                      const std::string_view class_get_type,
                                      void* class_ptr,
                                      std::map<std::string_view, std::shared_ptr<UnityClass>>& container) -> void {
                const auto name_opt = details::invoke_dyn_library<const char*>(class_get_name, class_ptr);
                if (!name_opt) {
                    return;
                }

                const auto parent_class = details::invoke_dyn_library<void*>(class_get_parent, class_ptr);
                const auto name_space_opt = details::invoke_dyn_library<const char*>(class_get_namespace, class_ptr);

                auto field_callback = std::bind(try_load_field, class_ptr, std::placeholders::_1);
                auto method_callback = std::bind(try_load_method, class_ptr, std::placeholders::_1);

                const auto class_type = details::invoke_dyn_library<void*>(class_get_type, class_ptr);
                auto type = try_find_type(*class_type);

                auto ptr = std::bit_cast<std::uintptr_t>(class_ptr);
                const auto unity_class = std::make_shared<UnityClass>(ptr,
                                                                      assembly,
                                                                      type,
                                                                      *name_opt,
                                                                      parent_class ? std::bit_cast<std::uintptr_t>(*parent_class) : 0,
                                                                      name_space_opt ? *name_space_opt : "",
                                                                      field_callback,
                                                                      method_callback);
                details::unity_classes[ptr] = container[*name_opt] = unity_class;
            }

            inline auto try_load_class_il2cpp(const std::shared_ptr<UnityAssembly>& assembly, void* image_ptr, std::map<std::string_view, std::shared_ptr<UnityClass>>& container) -> void {
                const auto count = details::invoke_dyn_library<int>("il2cpp_image_get_class_count", image_ptr);
                for (auto i = 0; i < count; i++) {
                    const auto class_ptr = details::invoke_dyn_library<void*>("il2cpp_image_get_class", image_ptr, i);
                    if (class_ptr == nullptr) {
                        continue;
                    }

                    do_load_class(assembly, "il2cpp_class_get_name", "il2cpp_class_get_parent", "il2cpp_class_get_namespace", "il2cpp_class_get_type", *class_ptr, container);
                }
            }

            inline auto try_load_class_mono(const std::shared_ptr<UnityAssembly>& assembly, void* image_ptr, std::map<std::string_view, std::shared_ptr<UnityClass>>& container) -> void {
                const auto table = details::invoke_dyn_library<void*>("mono_image_get_table_info", image_ptr, 2);
                if (!table) {
                    return;
                }
                const auto count = details::invoke_dyn_library<int>("mono_table_info_get_rows", *table);
                for (auto i = 0; i < count; i++) {
                    const auto class_ptr = details::invoke_dyn_library<void*>("mono_class_get", image_ptr, 0x02000000 | (i + 1));
                    if (class_ptr == nullptr) {
                        continue;
                    }

                    do_load_class(assembly, "mono_class_get_name", "mono_class_get_parent", "mono_class_get_namespace", "mono_class_get_type", *class_ptr, container);
                }
            }

            inline auto try_load_class(const std::string_view assembly_name, void* assembly_ptr, void* image_ptr, std::map<std::string_view, std::shared_ptr<UnityClass>>& container) -> void {
                const auto assembly = details::unity_assembly[assembly_name];
                if (details::mode == UnityMode::IL2CPP) {
                    try_load_class_il2cpp(assembly, image_ptr, container);
                } else {
                    try_load_class_mono(assembly, image_ptr, container);
                }
            }

            inline auto do_load_assembly(void* assembly_ptr, const std::string_view assembly_get_image, const std::string_view image_get_name) -> void {
                if (assembly_ptr == nullptr) {
                    return;
                }

                const auto image_opt = details::invoke_dyn_library<void*>(assembly_get_image, assembly_ptr);
                if (!image_opt) {
                    return;
                }

                const auto name_opt = details::invoke_dyn_library<const char*>(image_get_name, *image_opt);
                if (!name_opt) {
                    return;
                }
                const auto callback = std::bind(try_load_class, *name_opt, assembly_ptr, *image_opt, std::placeholders::_1);
                const auto assembly = std::make_shared<UnityAssembly>(*name_opt, std::bit_cast<std::uintptr_t>(assembly_ptr), callback);
                details::unity_assembly[*name_opt] = assembly;
            }

            inline auto try_load_assembly_il2cpp() -> void {
                std::size_t size = 0;
                const auto result = details::invoke_dyn_library<void**>("il2cpp_domain_get_assemblies", details::unity_domain, &size);
                if (!result) {
                    return;
                }

                for (auto* const assembly_ptr : std::span{*result, size}) {
                    do_load_assembly(assembly_ptr, "il2cpp_assembly_get_image", "il2cpp_image_get_name");
                }
            }

            inline auto try_load_assembly_mono() -> void {
                auto callback = [](void* assembly_ptr, void* /* unused */) -> void {
                    do_load_assembly(assembly_ptr, "mono_assembly_get_image", "mono_image_get_name");
                };
                details::invoke_dyn_library<void*, void(*)(void*, void*), void*>("mono_assembly_foreach", callback, nullptr);
            }
        } // namespace impl

        inline auto try_load_assembly() -> void {
            if (details::mode == UnityMode::IL2CPP) {
                impl::try_load_assembly_il2cpp();
            } else {
                impl::try_load_assembly_mono();
            }
        }

        inline auto fix_class_depend() -> void {
            for (const auto& assembly : details::unity_assembly | std::views::values) {
                for (const auto& unity_class : assembly->content()) {
                    const auto class_ptr = unity_class.lock();
                    class_ptr->native_parent = details::unity_classes[class_ptr->native_parent_ptr];
                    for (const auto& unity_field : class_ptr->unity_filed | std::views::values) {
                        unity_field->native_class = details::unity_classes[unity_field->native_class_ptr];
                    }
                }
            }
        }

        inline auto fix_type_depend() -> void {}
    } // namespace process

    inline auto set_params(const UnityMode unity_mode, void* module_handle) -> void {
        details::mode = unity_mode;
        details::module_handle = module_handle;
    }

    inline auto update() -> bool {
        details::unity_assembly.clear();
        details::unity_types.clear();
        details::unity_classes.clear();

        if (!details::update_domain()) {
            return false;
        }
        if (!details::thread_attach()) {
            return false;
        }

        process::try_load_assembly();
        process::fix_class_depend();
        process::fix_type_depend();
        return true;
    }

    inline auto unload() -> void {
        details::unity_assembly.clear();
        details::unity_types.clear();

        details::thread_detach();
    }

    inline auto find_assembly(const std::string_view name) -> std::optional<std::weak_ptr<UnityAssembly>> {
        if (!details::unity_assembly.contains(name)) {
            return std::nullopt;
        }
        return details::unity_assembly[name];
    }

    namespace api {
        class Object {};

        class Transform {};
    } // namespace api
} // namespace unity
