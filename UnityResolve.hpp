#pragma once
#include <concepts>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <type_traits>

#define UTYPE(type) unity::TypeIdentity<type>{}

namespace unity {
    enum class UnityMode : std::uint8_t { IL2CPP, MONO };

    class UnityType;
    class UnityArgs;
    class UnityClass;
    class UnityField;
    class UnityMethod;
    class UnityAssembly;

    template<typename T>
    struct TypeIdentity {
        using Type = T;
    };

    template<typename T> concept ClassMember = std::is_same_v<T, UnityField> || std::is_same_v<T, UnityMethod>;

    class UnityAssembly final {
        std::uint64_t native_ptr{};
        std::string native_name;

        mutable std::shared_mutex mutex;
        std::map<std::string, std::shared_ptr<UnityClass>> unity_class;
    public:
        auto name() const noexcept -> const std::string& {
            return native_name;
        }

        auto ptr() const noexcept -> std::uint64_t {
            return native_ptr;
        }

        auto operator[](const std::string& class_name) const -> std::optional<std::weak_ptr<UnityClass>> {
            std::shared_lock _(mutex);
            const auto it = unity_class.find(class_name);
            if (it == unity_class.end()) {
                return std::nullopt;
            }
            return it->second;
        }
    };

    class UnityClass final {
        std::uint64_t native_ptr{};
        std::weak_ptr<UnityAssembly> native_assembly;

        std::string native_name;
        std::string native_parent;
        std::string native_namespace;

        mutable std::shared_mutex mutex;
        std::map<std::string, std::shared_ptr<UnityField>> unity_filed;
        std::map<std::string, std::shared_ptr<UnityMethod>> unity_method;
    public:
        auto name() const noexcept -> const std::string& {
            return native_name;
        }

        auto ptr() const noexcept -> std::uint64_t {
            return native_ptr;
        }

        auto parent() const noexcept -> const std::string& {
            return native_parent;
        }

        auto name_space() const noexcept -> const std::string& {
            return native_namespace;
        }

        auto assembly() const noexcept -> std::weak_ptr<UnityAssembly> {
            return native_assembly;
        }

        template<ClassMember T>
        auto operator[](TypeIdentity<T> /* unused */, const std::string& class_name) const -> std::optional<std::weak_ptr<T>> {
            std::shared_lock _(mutex);

            const auto& container = get_container<T>();
            const auto it = container.find(class_name);
            if (it != container.end()) {
                return it->second;
            }
            return std::nullopt;
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
        std::uint64_t native_ptr{};

        std::weak_ptr<UnityClass> native_class;
        std::shared_ptr<UnityType> native_type;

        std::string native_name;
        std::uint32_t native_offset{};

        bool native_is_static{};
    public:
        auto name() const noexcept -> const std::string& {
            return native_name;
        }

        auto ptr() const noexcept -> std::uint64_t {
            return native_ptr;
        }

        auto type() const noexcept -> std::weak_ptr<UnityType> {
            return native_type;
        }

        auto offset() const noexcept -> std::uint32_t {
            return native_offset;
        }

        auto parent() const noexcept -> std::weak_ptr<UnityClass> {
            return native_class;
        }

        auto is_static() const noexcept -> bool {
            return native_is_static;
        }
    };

    class UnityArgs {
        mutable std::shared_mutex mutex;
        std::map<std::string, std::shared_ptr<UnityType>> args;
    public:
        auto count() const noexcept -> size_t {
            return args.size();
        }

        auto operator[](const std::string& arg_name) -> std::optional<std::weak_ptr<UnityType>> {
            std::shared_lock _(mutex);
            const auto it = args.find(arg_name);
            if (it == args.end()) {
                return std::nullopt;
            }
            return it->second;
        }
    };

    class UnityMethod final {
        std::uint64_t native_ptr{};

        std::weak_ptr<UnityClass> native_class;
        std::shared_ptr<UnityType> native_type;

        std::string native_name;
        std::uint32_t native_offset{};

        UnityArgs native_args;

        bool native_is_static{};
    public:
        auto name() const noexcept -> const std::string& {
            return native_name;
        }

        auto ptr() const noexcept -> std::uint64_t {
            return native_ptr;
        }

        auto type() const noexcept -> std::weak_ptr<UnityType> {
            return native_type;
        }

        auto args() const noexcept -> const UnityArgs& {
            return native_args;
        }

        auto offset() const noexcept -> std::uint32_t {
            return native_offset;
        }

        auto parent() const noexcept -> std::weak_ptr<UnityClass> {
            return native_class;
        }

        auto is_static() const noexcept -> bool {
            return native_is_static;
        }
    };
} // namespace unity
