#include <cobalt/ecs/system.hpp>

#include <gtest/gtest.h>

using namespace cobalt;

struct my_resource {
    std::string name;
};

struct counter {
    int x, y;
};

struct pos {
    int x, y;
};

TEST(system, system_state) {
    my_resource resource{ "resource" };
    counter count{ 0, 0 };

    ecs::registry registry;
    registry.set_resource<my_resource>(resource);
    registry.set_resource<counter>(count);

    registry.create<pos>({ 1, 2 });
    registry.create<pos>({ 2, 5 });

    ecs::system_executor system(registry, [](ecs::view<pos&> view, const my_resource& resource, counter& count) {
        view.each([&](const auto& p) { count.x += p.x; });
    });

    system.run();

    EXPECT_EQ(registry.get_resource<counter>().x, 3);
}