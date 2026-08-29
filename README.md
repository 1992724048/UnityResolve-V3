# UnityResolve-V3
## 状态: 开发中 (WIP)

### [GLM Library](https://github.com/g-truc/glm)

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
