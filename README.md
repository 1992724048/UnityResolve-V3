# UnityResolve-V3 (WIP)

``` c++
UNITY_ACCESS;

namespace {
    class Player : public Class {
    public:
        inline static Field<int> hp;
        inline static Method<void, Player*, int> set_hp;
        inline static Method<void, int> set_hp_static;
        inline static Property<int, Player> hp_access;
    };

    auto use() -> void {
        auto assembly_result = unity::find_assembly("Test.dll");
        auto& assembly = *assembly_result->lock();

        auto class_result = assembly["Player"];
        auto& class_ = *class_result->lock();

        auto& hp_field = *class_[UTYPE(unity::UnityField), "hp"]->lock();

        auto method = UTYPE(unity::UnityMethod);

        auto& hp_set_field = *class_[method, "set_hp"]->lock();
        auto& hp_static_field = *class_[method, "set_hp_static"]->lock();
        auto& hp_field_set = *class_[method, "hp_access_set"]->lock();
        auto& hp_field_get = *class_[method, "hp_access_get"]->lock();

        Player::hp[hp_field.offset()];
        Player::set_hp[hp_set_field.call()];
        Player::set_hp_static[hp_static_field.call()];
        Player::hp_access[hp_field_get.call(), hp_field_set.call()];

        Player player;
        player[player.hp] = 114514;
        player[player.set_hp](114514);
        Player::set_hp_static(114514);
        player[player.hp_access].set(114514);
        player[Player::hp_access].get();
    }
}
```