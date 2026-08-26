#pragma once
#include <algorithm>
#include <bitset>
#include <chrono>
#include <codecvt>
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

#define UTYPE(type) unity::util::TypeIdentity<type>{}
#define UNITY_ACCESS using namespace unity::access;

namespace unity {
    enum class UnityMode : std::uint8_t { IL2CPP, MONO };

    class UnityType;
    class UnityClass;
    class UnityField;
    class UnityMethod;
    class UnityAssembly;
    class UnityMethodArgs;

    namespace access {
        template<std::size_t Offset, typename T>
        struct AT {
            explicit(false) operator T&() {
                return *ptr();
            }

            explicit(false) operator const T&() {
                return *ptr();
            }

            auto get() -> T& {
                return *ptr();
            }

            auto ptr() -> T* {
                return std::bit_cast<T*>(std::bit_cast<std::uintptr_t>(this) + Offset);
            }
        };

        template<std::size_t Offset, typename Func>
        struct FN {
            template<typename... Args>
            auto operator()(Args&&... args) const -> decltype(auto) {
                auto func = std::bit_cast<Func*>(std::bit_cast<std::uintptr_t>(this) + Offset);
                return func(std::forward<Args>(args)...);
            }
        };

        template<typename T>
        struct Field {
        private:
            std::size_t offset_{};
        public:
            auto operator[](const std::size_t offset) -> void {
                this->offset_ = offset;
            }

            [[nodiscard]] auto offset() const -> std::size_t {
                return offset_;
            }
        };

        template<typename Ret, typename... Args>
        struct Method {
        private:
            std::function<Ret(Args...)> func;
        public:
            Method() = default;

            explicit(false) Method(const std::uintptr_t address) {
                operator[](address);
            }

            auto operator[](const std::uintptr_t address) -> void {
                this->func = std::bit_cast<Ret(UNITY_CALLING_CONVENTION*)(Args...)>(address);
            }

            auto operator()(Args&&... args) -> Ret {
                return func(std::forward<Args>(args)...);
            }

            [[nodiscard]] auto function() const -> const std::function<Ret(Args...)>& {
                return func;
            }
        };

        template<typename T, typename C = void>
        struct Property {
        private:
            using GetterFunc = std::conditional_t<std::is_void_v<C>, std::function<T()>, std::function<T(C*)>>;
            using SetterFunc = std::conditional_t<std::is_void_v<C>, std::function<void(T)>, std::function<void(C*, T)>>;
            GetterFunc get_;
            SetterFunc set_;
        public:
            Property() = default;

            Property(GetterFunc get, SetterFunc set) :
                get_{std::move(get)},
                set_{std::move(set)} {}

            auto operator[](const std::uintptr_t get_addr, const std::uintptr_t set_addr) -> void {
                if constexpr (std::is_void_v<C>) {
                    get_ = std::bit_cast<T(UNITY_CALLING_CONVENTION*)()>(get_addr);
                    set_ = std::bit_cast<void(UNITY_CALLING_CONVENTION*)(T)>(set_addr);
                } else {
                    get_ = std::bit_cast<T(UNITY_CALLING_CONVENTION*)(C*)>(get_addr);
                    set_ = std::bit_cast<void(UNITY_CALLING_CONVENTION*)(C*, T)>(set_addr);
                }
            }

            auto get() -> T requires std::is_void_v<C> {
                return get_();
            }

            auto set(T value) -> void requires std::is_void_v<C> {
                set_(std::move(value));
            }

            auto get(C* instance) -> T requires (!std::is_void_v<C>) {
                return get_(instance);
            }

            auto set(C* instance, T value) -> void requires (!std::is_void_v<C>) {
                set_(instance, std::move(value));
            }

            [[nodiscard]] auto function() const -> std::pair<const GetterFunc&, const SetterFunc&> {
                return {get_, set_};
            }
        };

        namespace helper {
            template<typename T>
            struct PropertyHelper {
            private:
                std::function<T()> get_;
                std::function<void(T)> set_;
            public:
                PropertyHelper(std::function<T()> get, std::function<void(T)> set) :
                    get_{std::move(get)},
                    set_{std::move(set)} {}

                auto get() -> T {
                    return get_();
                }

                auto set(T value) -> void {
                    set_(value);
                }
            };
        } // namespace helper

        struct Class {
            template<typename T>
            auto operator[](const Field<T>& field) -> T& {
                return *std::bit_cast<T*>(std::bit_cast<std::uintptr_t>(this) + field.offset());
            }

            template<typename Ret, typename... Args>
            auto operator[](Method<Ret, Args...>& method) {
                if constexpr (sizeof...(Args) > 0) {
                    using FirstArg = std::tuple_element_t<0, std::tuple<Args...>>;
                    if constexpr (std::is_pointer_v<FirstArg>) {
                        return std::bind_front(method.function(), static_cast<FirstArg>(this));
                    } else {
                        static_assert(std::is_pointer_v<FirstArg>, "Method's first argument must be a pointer type for member-like binding");
                    }
                } else {
                    static_assert(sizeof...(Args) > 0, "Method requires at least one argument (the object pointer)");
                }
            }

            template<typename T, typename C>
            auto operator[](Property<T, C>& property) -> helper::PropertyHelper<T> {
                auto [get_func, set_func] = property.function();
                if constexpr (std::is_void_v<C>) {
                    return helper::PropertyHelper<T>{std::move(get_func), std::move(set_func)};
                } else {
                    return helper::PropertyHelper<T>{std::bind_front(get_func, static_cast<C*>(this)), std::bind_front(set_func, static_cast<C*>(this))};
                }
            }
        };
    } // namespace access

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

    namespace attributes {
        // https://github.com/dotnet/runtime/blob/main/src/libraries/System.Private.CoreLib/src/System/Reflection/TypeAttributes.cs
        enum class TypeAttributes {
            VisibilityMask     = 0x00000007,
            NotPublic          = 0x00000000,
            Public             = 0x00000001,
            NestedPublic       = 0x00000002,
            NestedPrivate      = 0x00000003,
            NestedFamily       = 0x00000004,
            NestedAssembly     = 0x00000005,
            NestedFamANDAssem  = 0x00000006,
            NestedFamORAssem   = 0x00000007,
            LayoutMask         = 0x00000018,
            AutoLayout         = 0x00000000,
            SequentialLayout   = 0x00000008,
            ExplicitLayout     = 0x00000010,
            ExtendedLayout     = 0x00000018,
            ClassSemanticsMask = 0x00000020,
            Class              = 0x00000000,
            Interface          = 0x00000020,
            Abstract           = 0x00000080,
            Sealed             = 0x00000100,
            SpecialName        = 0x00000400,
            Import             = 0x00001000,
            Serializable       = 0x00002000,
            WindowsRuntime     = 0x00004000,
            StringFormatMask   = 0x00030000,
            AnsiClass          = 0x00000000,
            UnicodeClass       = 0x00010000,
            AutoClass          = 0x00020000,
            CustomFormatClass  = 0x00030000,
            CustomFormatMask   = 0x00C00000,
            BeforeFieldInit    = 0x00100000,
            RTSpecialName      = 0x00000800,
            HasSecurity        = 0x00040000,
            ReservedMask       = 0x00040800,
        };

        // https://github.com/dotnet/runtime/blob/main/src/libraries/System.Private.CoreLib/src/System/Reflection/MethodAttributes.cs
        enum class MethodAttributes : std::uint16_t {
            MemberAccessMask      = 0x0007,
            PrivateScope          = 0x0000,
            Private               = 0x0001,
            FamANDAssem           = 0x0002,
            Assembly              = 0x0003,
            Family                = 0x0004,
            FamORAssem            = 0x0005,
            Public                = 0x0006,
            Static                = 0x0010,
            Final                 = 0x0020,
            Virtual               = 0x0040,
            HideBySig             = 0x0080,
            CheckAccessOnOverride = 0x0200,
            VtableLayoutMask      = 0x0100,
            ReuseSlot             = 0x0000,
            NewSlot               = 0x0100,
            Abstract              = 0x0400,
            SpecialName           = 0x0800,
            PinvokeImpl           = 0x2000,
            UnmanagedExport       = 0x0008,
            RTSpecialName         = 0x1000,
            HasSecurity           = 0x4000,
            RequireSecObject      = 0x8000,
            ReservedMask          = 0xd000,
        };

        // https://github.com/dotnet/runtime/blob/main/src/libraries/System.Private.CoreLib/src/System/Reflection/FieldAttributes.cs
        enum class FieldAttributes : std::uint16_t {
            FieldAccessMask = 0x0007,
            PrivateScope    = 0x0000,
            Private         = 0x0001,
            FamANDAssem     = 0x0002,
            Assembly        = 0x0003,
            Family          = 0x0004,
            FamORAssem      = 0x0005,
            Public          = 0x0006,
            Static          = 0x0010,
            InitOnly        = 0x0020,
            Literal         = 0x0040,
            NotSerialized   = 0x0080,
            SpecialName     = 0x0200,
            PinvokeImpl     = 0x2000,
            RTSpecialName   = 0x0400,
            HasFieldMarshal = 0x1000,
            HasDefault      = 0x8000,
            HasFieldRVA     = 0x0100,
            ReservedMask    = 0x9500,
        };
    } // namespace attributes

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
            return unity_class | std::views::values | std::views::transform([](const std::shared_ptr<UnityClass>& ptr)-> std::weak_ptr<UnityClass> {
                return ptr;
            });
        }

        auto operator[](const std::string_view& class_name) const -> std::optional<std::weak_ptr<UnityClass>> {
            return util::try_find(mutex, class_name, unity_class);
        }
    };

    namespace api::csharp {
        class Type;
    }

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

        [[nodiscard]] auto type() const noexcept -> api::csharp::Type* {
            return std::bit_cast<api::csharp::Type*>(native_object);
        }
    };

    namespace process {
        auto fix_class_depend() -> void;
    } // namespace process

    template<typename T> concept ClassMember = std::is_same_v<T, UnityField> || std::is_same_v<T, UnityMethod>;

    class UnityField final {
        std::uintptr_t native_ptr{};
        std::uintptr_t native_class_ptr{};

        std::weak_ptr<UnityClass> native_class;
        std::weak_ptr<UnityType> native_type;

        std::string_view native_name;
        std::size_t native_offset{};

        std::bitset<32> native_flags;
        bool native_is_static{};

        friend auto process::fix_class_depend() -> void;
    public:
        UnityField(const std::uintptr_t native_ptr,
                   const std::uintptr_t native_class_ptr,
                   const std::weak_ptr<UnityType>& native_type,
                   const std::string_view& native_name,
                   const std::size_t native_offset,
                   const std::bitset<32> native_flags) :
            native_ptr{native_ptr},
            native_class_ptr{native_class_ptr},
            native_type{native_type},
            native_name{native_name},
            native_offset{native_offset},
            native_flags{native_flags} {
            native_is_static = flags(attributes::FieldAttributes::Static);
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

        [[nodiscard]] auto flags(attributes::FieldAttributes attributes) const noexcept -> bool {
            return native_flags.test(static_cast<uint32_t>(attributes));
        }

        [[nodiscard]] auto is_static() const noexcept -> bool {
            return native_is_static;
        }
    };

    class UnityMethodArgs {
        mutable std::shared_mutex mutex;
        std::map<std::string_view, std::weak_ptr<UnityType>> args;
        std::vector<std::pair<std::weak_ptr<UnityType>, std::string_view>> param_vec;
    public:
        UnityMethodArgs() = default;
        ~UnityMethodArgs() = default;

        explicit UnityMethodArgs(const std::map<std::string_view, std::shared_ptr<UnityType>>& types, std::vector<std::pair<std::weak_ptr<UnityType>, std::string_view>>& params) :
            args(types | std::views::transform([](const auto& pair) -> auto {
                return std::pair{pair.first, std::weak_ptr<UnityType>(pair.second)};
            }) | std::ranges::to<std::map>()),
            param_vec{std::move(params)} {}

        UnityMethodArgs(UnityMethodArgs&& other) noexcept :
            args(std::move(other.args)),
            param_vec(std::move(other.param_vec)) {}

        UnityMethodArgs(const UnityMethodArgs& other) = delete;
        auto operator=(const UnityMethodArgs& other) -> UnityMethodArgs& = delete;

        auto operator=(UnityMethodArgs&& other) noexcept -> UnityMethodArgs& {
            args = std::move(other.args);
            param_vec = std::move(other.param_vec);
            return *this;
        }

        auto count() const noexcept -> std::size_t {
            return args.size();
        }

        auto map() const noexcept -> const std::map<std::string_view, std::weak_ptr<UnityType>>& {
            return args;
        }

        auto params() const noexcept -> const std::vector<std::pair<std::weak_ptr<UnityType>, std::string_view>>& {
            return param_vec;
        }
    };

    class UnityMethod final {
        std::uintptr_t native_ptr{};
        std::uintptr_t native_call_ptr{};
        std::uintptr_t native_class_ptr{};

        std::weak_ptr<UnityClass> native_class;
        std::weak_ptr<UnityType> native_type;

        std::string_view native_name;
        UnityMethodArgs native_args;

        std::bitset<32> native_flags;
        bool native_is_static{};

        bool native_is_compile{};
        std::function<std::optional<void*>()> compile_call;

        friend auto process::fix_class_depend() -> void;
    public:
        UnityMethod(const std::uintptr_t native_ptr,
                    const std::uintptr_t native_call_ptr,
                    const std::uintptr_t native_class_ptr,
                    const std::weak_ptr<UnityType>& native_type,
                    const std::string_view& native_name,
                    const std::function<UnityMethodArgs()>& args_callback,
                    const std::function<std::optional<void*>()>& compile,
                    const std::bitset<32>& native_flags) :
            native_ptr{native_ptr},
            native_call_ptr{native_call_ptr},
            native_class_ptr{native_class_ptr},
            native_type{native_type},
            native_name{native_name},
            native_flags{native_flags},
            compile_call{compile} {
            native_args = args_callback();
            native_is_static = flags(attributes::MethodAttributes::Static);
            native_is_compile = !compile;
        }

        auto name() const noexcept -> std::string_view {
            return native_name;
        }

        auto ptr() const noexcept -> std::uintptr_t {
            return native_ptr;
        }

        auto call() -> std::uintptr_t {
            if (native_is_compile) {
                return native_call_ptr;
            }
            compile();
            return native_call_ptr;
        }

        auto type() const noexcept -> std::weak_ptr<UnityType> {
            return native_type;
        }

        auto args() const noexcept -> const UnityMethodArgs& {
            return native_args;
        }

        auto parent() const noexcept -> std::weak_ptr<UnityClass> {
            return native_class;
        }

        auto flags(attributes::MethodAttributes attributes) const noexcept -> bool {
            return native_flags.test(static_cast<uint32_t>(attributes));
        }

        auto is_static() const noexcept -> bool {
            return native_is_static;
        }

        template<typename Return, typename... Args>
        auto invoke(Args&&... args) -> Return {
            // TODO: use 'std::start_lifetime_as' in c++23 if compiler support
            return std::bit_cast<Return(UNITY_CALLING_CONVENTION*)(Args...)>(call())(std::forward<Args>(args)...);
        }
    private:
        auto compile() -> void {
            const auto result = compile_call();
            if (!result) {
                throw std::runtime_error("nullptr");
            }
            native_call_ptr = std::bit_cast<std::uintptr_t>(*result);
            native_is_compile = result.has_value();
        }
    };

    class UnityClass final {
        std::uintptr_t native_ptr{};
        std::uintptr_t native_parent_ptr{};
        std::weak_ptr<UnityAssembly> native_assembly;
        std::weak_ptr<UnityType> native_type;
        std::weak_ptr<UnityClass> native_parent;

        std::string_view native_name;
        std::string_view native_namespace;

        std::bitset<32> native_flags;

        mutable std::shared_mutex mutex;
        std::map<std::string_view, std::shared_ptr<UnityField>> unity_filed;
        std::multimap<std::string_view, std::shared_ptr<UnityMethod>> unity_method;

        friend auto process::fix_class_depend() -> void;
    public:
        UnityClass(const std::uintptr_t native_ptr,
                   const std::weak_ptr<UnityAssembly>& native_assembly,
                   const std::weak_ptr<UnityType>& native_type,
                   const std::string_view native_name,
                   const std::uintptr_t native_parent_ptr,
                   const std::string_view native_namespace,
                   const std::bitset<32> native_flags,
                   const std::function<void(std::map<std::string_view, std::shared_ptr<UnityField>>&)>& field_callback,
                   const std::function<void(std::multimap<std::string_view, std::shared_ptr<UnityMethod>>&)>& method_callback) :
            native_ptr{native_ptr},
            native_parent_ptr{native_parent_ptr},
            native_assembly{native_assembly},
            native_type{native_type},
            native_name{native_name},
            native_namespace{native_namespace},
            native_flags{native_flags} {
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

        auto flags(attributes::TypeAttributes attributes) const noexcept -> bool {
            return native_flags.test(static_cast<uint32_t>(attributes));
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
            return get_container<T>() | std::views::values | std::views::transform([](const std::shared_ptr<T>& ptr)-> std::weak_ptr<T> {
                return ptr;
            });
        }

        template<ClassMember T>
        auto operator[](util::TypeIdentity<T> /* unused */, const std::string_view name, const std::vector<std::string_view> args_type = {}) const -> std::optional<std::weak_ptr<T>> {
            if constexpr (std::is_same_v<T, UnityField>) {
                return util::try_find(mutex, name, unity_filed);
            } else {
                std::shared_lock _(mutex);
                const auto [fst, snd] = unity_method.equal_range(name);
                if (fst == snd) {
                    return std::nullopt;
                }
                if (args_type.empty()) {
                    return fst->second;
                }
                return find_with_args(args_type, std::ranges::subrange(fst, snd));
            }
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

        using MethodIt = decltype(unity_method)::const_iterator;

        static auto find_with_args(const std::vector<std::string_view>& args, const std::ranges::subrange<MethodIt>& range) -> std::optional<std::weak_ptr<UnityMethod>> {
            auto matches = [&](const auto& entry) {
                const auto& params = entry.second->args().params();
                if (params.size() != args.size()) {
                    return false;
                }

                return std::ranges::all_of(std::views::iota(0u, params.size()),
                                           [&](std::size_t i) {
                                               auto type_ptr = params[i].first.lock();
                                               return type_ptr && type_ptr->name() == args[i];
                                           });
            };

            const auto it = std::ranges::find_if(range, matches);
            if (it != range.end()) {
                return it->second;
            }
            return std::nullopt;
        }
    };

    namespace details {
        inline UnityMode mode;
        inline void* module_handle;
        inline void* unity_domain;
        inline std::map<std::string_view, std::shared_ptr<UnityAssembly>> unity_assembly;
        inline std::map<std::uintptr_t, std::shared_ptr<UnityType>> unity_types;
        inline std::map<std::uintptr_t, std::weak_ptr<UnityClass>> unity_classes;

        inline auto method = UTYPE(UnityMethod);
        inline auto field = UTYPE(UnityField);

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

            // TODO: use 'std::start_lifetime_as' in c++23 if compiler support
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
                using namespace std::chrono_literals;
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

        inline auto find_assembly_impl(const std::string_view name) -> std::optional<std::weak_ptr<UnityAssembly>> {
            if (!unity_assembly.contains(name)) {
                return std::nullopt;
            }
            return unity_assembly[name];
        }

        inline auto try_find_class(const std::string_view assembly_name, const std::string_view class_name) -> std::optional<std::weak_ptr<UnityClass>> {
            const auto assembly_result = find_assembly_impl(assembly_name);
            if (!assembly_result) {
                return std::nullopt;
            }
            const auto& assembly = *assembly_result->lock();
            return assembly[class_name];
        }

        template<typename Ret, typename... Args>
        auto try_find_method(const std::string_view assembly_name, const std::string_view class_name, const std::string_view method_name, const std::vector<std::string_view> args = {}) -> access::Method<Ret, Args...> {
            const auto class_result = try_find_class(assembly_name, class_name);
            if (!class_result) {
                return {0x0};
            }
            const auto& class_ = *class_result->lock();
            const auto method_result = class_[method, method_name, args];
            if (!method_result) {
                return {0x0};
            }
            return {method_result->lock()->call()};
        }

        template<typename T>
        auto new_gc_handle(T* obj, bool pinned = true) -> std::shared_ptr<T> {
            if (obj == nullptr) {
                return nullptr;
            }

            std::uint32_t handle = 0;
            auto result = details::invoke_dyn_library<uint32_t>(mode == UnityMode::IL2CPP ? "il2cpp_gchandle_new" : "mono_gchandle_new", obj, pinned);
            if (result) {
                handle = *result;
            }
            if (handle == 0) {
                return nullptr;
            }
            return std::shared_ptr<T>(obj,
                                      [handle](T* /*ptr*/) -> auto {
                                          details::invoke_dyn_library<void>(mode == UnityMode::IL2CPP ? "il2cpp_gchandle_free" : "mono_gchandle_free", handle);
                                      });
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

            inline auto try_load_args_il2cpp(void* method_ptr) -> UnityMethodArgs {
                const auto param_count_result = details::invoke_dyn_library<int>("il2cpp_method_get_param_count", method_ptr);
                if (!param_count_result || *param_count_result == 0) {
                    return {};
                }
                const int param_count = *param_count_result;
                std::map<std::string_view, std::shared_ptr<UnityType>> args;
                std::vector<std::pair<std::weak_ptr<UnityType>, std::string_view>> params;
                for (auto index = 0; index < param_count; index++) {
                    const auto arg_type = details::invoke_dyn_library<void*>("il2cpp_method_get_param", method_ptr, index);
                    const auto arg_name = details::invoke_dyn_library<const char*>("il2cpp_method_get_param_name", method_ptr, index);
                    if (!arg_name || !arg_type) {
                        continue;
                    }
                    auto& type = try_find_type(*arg_type);
                    args[*arg_name] = type;
                    params.push_back({type, *arg_name});
                }
                return UnityMethodArgs(args, params);
            }

            inline auto try_load_args_mono(void* method_ptr) -> UnityMethodArgs {
                const auto signature_ptr_result = details::invoke_dyn_library<void*>("mono_method_signature", method_ptr);
                if (!signature_ptr_result || *signature_ptr_result == nullptr) {
                    return {};
                }
                auto* signature = *signature_ptr_result;
                const auto param_count_result = details::invoke_dyn_library<int>("mono_signature_get_param_count", signature);
                if (!param_count_result || *param_count_result == 0) {
                    return {};
                }

                const int param_count = *param_count_result;
                std::map<std::string_view, std::shared_ptr<UnityType>> args;
                std::vector<std::pair<std::weak_ptr<UnityType>, std::string_view>> params;
                std::vector<char*> names(param_count);
                details::invoke_dyn_library<void>("mono_method_get_param_names", method_ptr, names.data());

                void* iter{nullptr};
                void* type_ptr{nullptr};
                int name_index{0};

                do {
                    const auto type_ptr_result = details::invoke_dyn_library<void*>("mono_signature_get_params", signature, &iter);
                    if (!type_ptr_result) {
                        continue;
                    }
                    type_ptr = *type_ptr_result;
                    auto& type = try_find_type(type_ptr);
                    args[names[name_index]] = type;
                    params.push_back({type, names[name_index]});
                    name_index++;
                } while (type_ptr != nullptr);
                return UnityMethodArgs(args, params);
            }

            inline auto do_load_method(void* method_ptr,
                                       void* method_address,
                                       void* method_type_ptr,
                                       void* class_ptr,
                                       int flags,
                                       const std::string_view name,
                                       std::multimap<std::string_view, std::shared_ptr<UnityMethod>>& container) -> void {
                auto type = try_find_type(method_type_ptr);
                auto callback = std::bind(details::mode == UnityMode::IL2CPP ? try_load_args_il2cpp : try_load_args_mono, method_ptr);
                auto compile = [method_ptr] -> std::optional<void*> {
                    return details::invoke_dyn_library<void*>("mono_compile_method", method_ptr);
                };
                const auto method = std::make_shared<UnityMethod>(std::bit_cast<std::uintptr_t>(method_ptr),
                                                                  std::bit_cast<std::uintptr_t>(method_address),
                                                                  std::bit_cast<std::uintptr_t>(class_ptr),
                                                                  type,
                                                                  name,
                                                                  callback,
                                                                  compile,
                                                                  flags);
                container.emplace(name, method);
            }

            inline auto try_load_method_il2cpp(void* class_ptr, std::multimap<std::string_view, std::shared_ptr<UnityMethod>>& container) -> void {
                void* iter{nullptr};
                void* method_ptr{nullptr};
                do {
                    const auto method_ptr_result = details::invoke_dyn_library<void*>("il2cpp_class_get_methods", class_ptr, &iter);
                    if (!method_ptr_result) {
                        continue;
                    }

                    method_ptr = *method_ptr_result;

                    int tmp;
                    auto name = details::invoke_dyn_library<const char*>("il2cpp_method_get_name", method_ptr);
                    auto type = details::invoke_dyn_library<void*>("il2cpp_method_get_return_type", method_ptr);
                    auto flags = details::invoke_dyn_library<int>("il2cpp_method_get_flags", method_ptr, &tmp);

                    if (!name || !type || !flags || *name == nullptr || *type == nullptr) {
                        continue;
                    }

                    do_load_method(method_ptr, *static_cast<void**>(method_ptr), *type, class_ptr, *flags, *name, container);
                } while (method_ptr != nullptr);
            }

            inline auto try_load_method_mono(void* class_ptr, std::multimap<std::string_view, std::shared_ptr<UnityMethod>>& container) -> void {
                void* iter{nullptr};
                void* method_ptr{nullptr};
                do {
                    const auto method_ptr_result = details::invoke_dyn_library<void*>("mono_class_get_methods", class_ptr, &iter);
                    if (!method_ptr_result) {
                        continue;
                    }

                    method_ptr = *method_ptr_result;
                    const auto signature_ptr_result = details::invoke_dyn_library<void*>("mono_method_signature", method_ptr);
                    if (!signature_ptr_result) {
                        continue;
                    }
                    auto* signature = *signature_ptr_result;

                    int tmp;
                    auto name = details::invoke_dyn_library<const char*>("mono_method_get_name", method_ptr);
                    auto type = details::invoke_dyn_library<void*>("mono_signature_get_return_type", signature);
                    auto flags = details::invoke_dyn_library<int>("mono_method_get_flags", method_ptr, &tmp);

                    if (!name || !type || !flags || *name == nullptr || *type == nullptr) {
                        continue;
                    }

                    do_load_method(method_ptr, nullptr, *type, class_ptr, *flags, *name, container);
                } while (method_ptr != nullptr);
            }

            inline auto try_load_method(void* class_ptr, std::multimap<std::string_view, std::shared_ptr<UnityMethod>>& container) -> void {
                if (details::mode == UnityMode::IL2CPP) {
                    try_load_method_il2cpp(class_ptr, container);
                } else {
                    try_load_method_mono(class_ptr, container);
                }
            }

            inline auto foreach_load_field(const std::string_view class_get_fields,
                                           const std::string_view field_get_name,
                                           const std::string_view field_get_type,
                                           const std::string_view field_get_offset,
                                           const std::string_view field_get_flags,
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
                    const auto field_flags = details::invoke_dyn_library<int>(field_get_flags, field_ptr);

                    if (!field_name || !field_type || !field_offset) {
                        continue;
                    }

                    auto type = try_find_type(*field_type);
                    const auto field = std::make_shared<UnityField>(std::bit_cast<std::uintptr_t>(field_ptr), std::bit_cast<std::uintptr_t>(class_ptr), type, *field_name, *field_offset, *field_flags);
                    container[*field_name] = field;
                } while (field_ptr != nullptr);
            }

            inline auto try_load_field(void* class_ptr, std::map<std::string_view, std::shared_ptr<UnityField>>& container) -> void {
                if (details::mode == UnityMode::IL2CPP) {
                    foreach_load_field("il2cpp_class_get_fields", "il2cpp_field_get_name", "il2cpp_field_get_type", "il2cpp_field_get_offset", "il2cpp_field_get_flags", class_ptr, container);
                } else {
                    foreach_load_field("mono_class_get_fields", "mono_field_get_name", "mono_field_get_type", "mono_field_get_offset", "mono_field_get_flags", class_ptr, container);
                }
            }

            inline auto do_load_class(const std::shared_ptr<UnityAssembly>& assembly,
                                      const std::string_view class_get_name,
                                      const std::string_view class_get_parent,
                                      const std::string_view class_get_namespace,
                                      const std::string_view class_get_type,
                                      const std::string_view class_get_flags,
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
                const auto class_flags = details::invoke_dyn_library<int>(class_get_flags, class_ptr);
                auto type = try_find_type(*class_type);

                auto ptr = std::bit_cast<std::uintptr_t>(class_ptr);
                const auto unity_class = std::make_shared<UnityClass>(ptr,
                                                                      assembly,
                                                                      type,
                                                                      *name_opt,
                                                                      parent_class ? std::bit_cast<std::uintptr_t>(*parent_class) : 0,
                                                                      *name_space_opt,
                                                                      *class_flags,
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

                    do_load_class(assembly, "il2cpp_class_get_name", "il2cpp_class_get_parent", "il2cpp_class_get_namespace", "il2cpp_class_get_type", "il2cpp_class_get_flags", *class_ptr, container);
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

                    do_load_class(assembly, "mono_class_get_name", "mono_class_get_parent", "mono_class_get_namespace", "mono_class_get_type", "mono_class_get_flags", *class_ptr, container);
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
                    if (class_ptr->native_parent_ptr != 0) {
                        class_ptr->native_parent = details::unity_classes[class_ptr->native_parent_ptr];
                    }
                    for (const auto& unity_field : class_ptr->unity_filed | std::views::values) {
                        unity_field->native_class = details::unity_classes[unity_field->native_class_ptr];
                    }
                    for (const auto& unity_method : class_ptr->unity_method | std::views::values) {
                        unity_method->native_class = details::unity_classes[unity_method->native_class_ptr];
                    }
                }
            }
        }
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
        return true;
    }

    inline auto unload() -> void {
        details::unity_assembly.clear();
        details::unity_types.clear();

        details::thread_detach();
    }

    inline auto find_assembly(const std::string_view name) -> std::optional<std::weak_ptr<UnityAssembly>> {
        return details::find_assembly_impl(name);
    }

    namespace api {
        UNITY_ACCESS;

        namespace csharp {
            class String;
            class Type;

            class Object : Class {
                union {
                    void* this_{nullptr};
                    void* vtable;
                } il2cpp_class;

                struct MonitorData* monitor{nullptr};
            public:
                auto get_type() -> Type* {
                    static auto func = details::try_find_method<Type*, Object*>("mscorlib.dll", "Object", "GetType");
                    return func(this);
                }

                auto to_string() -> String* {
                    static auto func = details::try_find_method<String*, Object*>("mscorlib.dll", "Object", "ToString");
                    return func(this);
                }

                auto get_hash_code() -> int {
                    static auto func = details::try_find_method<int, Object*>("mscorlib.dll", "Object", "GetHashCode");
                    return func(this);
                }
            };

            class Type {
            public:
                auto is_enum() -> bool {
                    static auto func = details::try_find_method<bool, Type*>("mscorlib.dll", "Type", "get_IsEnum");
                    return func(this);
                }
            };

            class String : public Object {
                std::int32_t length{0};
                std::array<wchar_t, 2> wchar_array{};
            public:
                [[nodiscard]] auto local() const -> std::string {
                    static std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
                    return converter.to_bytes(wchar_array.data());
                }

                explicit(false) operator std::string() const {
                    return local();
                }

                [[nodiscard]] auto size() const -> std::int32_t {
                    return length;
                }

                auto clear() -> void {
                    std::memset(wchar_array.data(), 0x00, length);
                }

                auto operator[](const uint32_t index) -> wchar_t {
                    return wchar_array[index];
                }

                auto operator[](const uint32_t index) const -> wchar_t {
                    return wchar_array[index];
                }

                static auto new_str(const std::string& str) -> std::shared_ptr<String> {
                    if (details::mode == UnityMode::IL2CPP) {
                        return details::new_gc_handle<String>(*details::invoke_dyn_library<String*>("il2cpp_string_new", str.data()));
                    }
                    return details::new_gc_handle<String>(*details::invoke_dyn_library<String*>("mono_string_new", details::unity_domain, str.data()));
                }
            };

            template<typename T, std::size_t Dim = 1>
            class Array : public Object {
                struct {
                    std::size_t length;
                    std::int32_t lower_bound;
                }* il2cpp_array_bounds{nullptr};

                std::size_t max_length{0};
                std::array<T, 1> content;
            public:
                auto bounds() const -> std::array<size_t, Dim> {
                    std::array<size_t, Dim> extents;
                    if constexpr (Dim == 1) {
                        extents[0] = max_length;
                    } else {
                        for (int i = 0; i < Dim; ++i) {
                            extents[i] = il2cpp_array_bounds[i].length;
                        }
                    }
                    return extents;
                }

                [[nodiscard]] auto size() const -> std::size_t {
                    return max_length;
                }

                auto local() -> std::vector<T> {
                    return std::vector<T>(content.data(), content.data() + max_length);
                }

                auto span() -> std::span<T> {
                    return std::span<T>(content.data(), max_length);
                }

                auto operator[](const uint32_t index) -> T& {
                    return content[index];
                }

                auto operator[](const uint32_t index) const -> const T& {
                    return content[index];
                }

                auto resize(std::int32_t size) -> void {
                    static auto func = details::try_find_method<void, Array*, std::int32_t>("mscorlib.dll", "Array", "Resize");
                    return func(this, size);
                }

                static auto new_arr(const std::shared_ptr<UnityClass>& class_, std::size_t size) -> std::shared_ptr<Array> {
                    if (details::mode == UnityMode::IL2CPP) {
                        return details::new_gc_handle<Array>(*details::invoke_dyn_library<Array*>("il2cpp_array_new", class_->ptr(), size));
                    }
                    return details::new_gc_handle<Array>(*details::invoke_dyn_library<Array*>("mono_array_new", details::unity_domain, class_->ptr(), size));
                }
            };

            template<typename T>
            class List : public Object {
                Array<T>* list{};
                std::int32_t length{};
                std::int32_t version{};
                struct Il2CppObject* sync_root{nullptr};
            public:
                auto local() -> std::list<T> {
                    return list->local() | std::ranges::to<std::list>();
                }

                auto span() -> std::span<T> {
                    return list->span();
                }

                [[nodiscard]] auto size() const -> std::size_t {
                    return static_cast<std::size_t>(length);
                }
            };

            template<typename TKey, typename TValue>
            class Dictionary : public Object {
                struct Entry {
                    std::int32_t hash_code;
                    std::int32_t next;
                    TKey key;
                    TValue value;
                };

                Array<std::int32_t>* buckets{nullptr};
                Array<Entry>* entries{nullptr};
                std::int32_t count{0};
                std::int32_t version{0};
                std::int32_t free_list{-1};
                std::int32_t free_count{0};
                Il2CppObject* comparer{nullptr};
            public:
                [[nodiscard]] auto size() const -> std::size_t {
                    return static_cast<std::size_t>(count);
                }

                auto local() const -> std::unordered_map<TKey, TValue> {
                    std::unordered_map<TKey, TValue> map;
                    if (entries == nullptr) {
                        return map;
                    }

                    for (const Entry& entry : entries->span()) {
                        if (entry.hash_code >= 0) {
                            map[entry.key] = entry.value;
                        }
                    }
                    return map;
                }

                auto operator[](const TKey& key) const -> std::optional<TValue&> {
                    for (auto& obj : entries->span()) {
                        if (obj.key == key) {
                            return obj.value;
                        }
                    }
                    return std::nullopt;
                }
            };
        } // namespace csharp

        namespace engine {
            class Object : public csharp::Object {
                std::uintptr_t cached_ptr{0};
            public:
                template<typename T>
                auto native() -> T* {
                    return std::bit_cast<T*>(cached_ptr);
                }
            };

            class GameObject : public Object {};

            class Component : public Object {};

            class Behaviour : public Component {};

            class Transform : public Component {};

            class Camera : public Behaviour {};
        }
    } // namespace api
} // namespace unity
