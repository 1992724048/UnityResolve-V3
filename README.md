# UnityResolve-V3

UnityResolve-V3 是一个用于在 C++ 中访问 Unity C# 类、字段、方法和属性的库。它提供了一个简单的接口，使开发者能够轻松地与 Unity 的 C# 代码进行交互。

---

## 相关依赖
- [GLM Library](https://github.com/g-truc/glm)
---

## 使用示例

### 初始化
- 通过 `unity::set_params` 设置 Unity 模式和模块句柄。
- 通过 `unity::update` 更新 UnityResolve 的状态。
``` c++
unity::set_params(unity::UnityMode::IL2CPP, GetModuleHandleW(L"GameAssembly.dll"));
unity::update();
```
### 访问 C# 类、字段、方法和属性

#### 宏定义

- `UTYPE(type)`：用于指定对象类型。
- `UNITY_ACCESS`：引入 `unity::access` 命名空间。
- `UNITY_UDL`：引入 `unity::udl` 命名空间。

``` c++
#define UTYPE(type) unity::util::TypeIdentity<type>{}
#define UNITY_ACCESS using namespace unity::access;
#define UNITY_UDL using namespace unity::udl;
```

#### UDL 支持

- UDL 用于简化 C# 类和字段的访问。
- 通过 `"_class"` 获取 C# 类。
- 通过 `"_field"` 获取 C# 字段。
- `:` 用于分隔程序集名称、类名称、字段名称。

``` c++
const auto class_result = "Test.dll:Player"_class;
const auto& name_filed = "Test.dll:Player:name"_field;
```
> 注意：`"_class"` 和 `"_field"` 返回是 `std::optional<std::weak_ptr<T>>` 请务必检查结果是否有效。

#### 获取 C# 类
``` c++
const auto class_result = "Test.dll:Player"_class;
// 等效于: unity::find_class("Test.dll", "Player");
```
#### 获取 C# 字段
``` c++
const auto& name_filed = "Test.dll:Player:name"_field;
// 等效于: unity::find_field("Test.dll", "Player", "name");
```

#### 获取 C# 方法、属性

- 其中 `UTYPE()` 用于指定获取的对象类型，支持以下类型：UnityMethod、 UnityField。
- C# 属性编译后会生成 `get_xxx` 和 `set_xxx` 方法，分别用于获取和设置属性值。

``` c++
const auto& hp_set_field = class_[UTYPE(unity::UnityMethod), "set_hp"];
```

---

### 使用访问器

- 使用 `UNITY_ACCESS` 后，可以通过 `Class` 的下标运算符访问字段、方法和属性。

#### 访问字段

- 使用 `[offset()]` 进行字段访问器初始化。
- 使用 `Class[]` 进行字段访问。
- 字段访问器返回的是 `T&`，其中 `T` 是字段类型。

``` c++
Field<unity::api::csharp::String*> name;
Player::name[name.offset()];

const auto& name_value = player[name]; // T&
```

#### 访问方法

- 使用 `[call()]` 进行方法访问器初始化。
- 使用 `Class[]` 进行非静态方法访问。
- 方法访问器返回的是 `Method<T, Args...>`，其中 `T` 是返回类型，`Args...` 是参数类型。
- 静态方法可以直接调用，而非静态方法需要通过对象调用。

``` c++
// 非静态方法
Method<void, Player*, int> set_hp;
Player::set_hp[hp_set_field.call()];

// 静态方法
Method<void, int> set_hp_static;
Player::set_hp_static[hp_static_field.call()];

player[set_hp](114514); // 调用非静态方法
set_hp_static(114514); // 调用静态方法
```

#### 访问属性

- 使用 `[call()]` 进行属性访问器初始化，需要提供 getter 和 setter 方法的调用，可以只提供其中一个，但不能都省略，且不能调用未提供的方法。
- 使用 `Class[]` 进行非静态属性访问。
- 属性访问器返回的是 `Property<T, Class>`，其中 `T` 是属性类型，`Class` 是类类型（默认 `void`）。
- 静态属性可以直接调用，而非静态属性需要通过对象调用。

``` c++
Property<int, Player> hp_access;
Player::hp_access[hp_field_get.call(), hp_field_set.call()];

player[hp_access] = 114514; // 设置属性值
int hp = player[hp_access]; // 获取属性值
```

### 简单示例

``` c++
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
```
