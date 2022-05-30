#pragma once

#include <cobalt/asl/sparse_map.hpp>
#include <cobalt/ecs/archetype.hpp>
#include <cobalt/ecs/entity.hpp>
#include <cobalt/ecs/entity_location.hpp>
#include <cobalt/ecs/event.hpp>
#include <cobalt/ecs/resource.hpp>

#include <any>

namespace cobalt::ecs {

/// @brief Registry is a container for all our entities and components. Components are stored in continuosly in memory
/// allowing for very fast iterations, a so called SoA approach. A set of unique components form an archetype, where
/// every entity is mapped to an archetype.
class registry {
public:
    /// @brief Creates a new entity in the world with components Args... attached and returns an ecs::entity that
    /// user can use to operate on the entity later, example:
    ///
    /// @code {.cpp}
    /// struct position {
    ///    int x;
    ///    int y;
    /// };
    ///
    /// int main() {
    ///     ecs::registry registry;
    ///     auto entity = registry.create<position>({ 1, 2 });
    ///     return 0;
    /// }
    /// @endcode
    ///
    /// @tparam Args Component types
    /// @param args components
    /// @return entity Entity to construct
    template<component... Args>
    entity create(Args&&... args) {
        auto entity = _entity_pool.create();
        auto archetype = _archetypes.ensure_archetype<Args...>();
        auto location = archetype->template emplace_back<Args...>(entity, std::forward<Args>(args)...);
        set_location(entity.id(), location);
        return entity;
    }

    /// @brief Destroy an entity
    ///
    /// @param ent Entity to destroy
    void destroy(entity ent) {
        ensure_alive(ent);
        auto location = get_location(ent.id());

        // returns the entity that has been moved to a new location
        auto moved = location.archetype->swap_erase(location);
        remove_location(ent.id());

        if (moved) {
            set_location(moved->id(), location);
        }

        _entity_pool.recycle(ent);
    }

    /// @brief Set component to an entity. It can either override a component value that is already assigned to an
    /// entity or it may construct a new once and assign to it. Note, such operation involves an archetype change
    /// which is a costly operation.
    ///
    /// @code {.cpp}
    /// struct position {
    ///    int x;
    ///    int y;
    /// };
    ///
    /// struct velocity {
    ///    int x;
    ///    int y;
    /// };
    ///
    /// int main() {
    ///     ecs::registry registry;
    ///     auto entity = registry.create<position>({ 1, 2 });
    ///     registry.set<position>(entity, 1, 2 );
    ///     return 0;
    /// }
    /// @endcode
    ///
    /// @tparam C Compone type
    /// @tparam Args Parameter pack, argument types to construct C from
    /// @param ent Entity to assign component to
    /// @param args Arguments to construct C from
    template<component C, typename... Args>
    void set(entity ent, Args&&... args) {
        ensure_alive(ent);
        auto& location = get_location(ent.id());
        auto*& archetype = location.archetype;

        if (archetype->contains<C>()) {
            archetype->template get<C&>(location) = C{ std::forward<Args>(args)... };
        } else {
            auto new_archetype = _archetypes.ensure_archetype_added<C>(archetype);
            auto [new_location, moved] = archetype->move(location, *new_archetype);

            auto ptr = std::addressof(new_archetype->template get<C&>(new_location));
            std::construct_at(ptr, std::forward<Args>(args)...);

            if (moved) {
                set_location(moved->id(), location);
            }

            archetype = new_archetype;
            set_location(ent.id(), new_location);
        }
    }

    /// @brief Remove component C from an entity. In case entity does not have component attached nothing is done
    /// and this method returns. In case component is removed it requires archetype change which is a costly
    /// operation.
    ///
    /// @tparam C Component type
    /// @param ent Entity to remove component from
    template<component C>
    void remove(entity ent) {
        ensure_alive(ent);
        auto entity_id = ent.id();
        auto& location = get_location(entity_id);
        auto*& archetype = location.archetype;

        if (!archetype->contains<C>()) {
            return;
        }
        auto new_archetype = _archetypes.ensure_archetype_removed<C>(archetype);
        auto [new_location, moved] = archetype->move(location, *new_archetype);
        if (moved) {
            set_location(moved->id(), location);
        }

        archetype = new_archetype;
        set_location(entity_id, new_location);
    }

    /// @brief Check if an entity is alive or not
    ///
    /// @param ent Entity
    /// @return true Alive
    /// @return false Dead
    [[nodiscard]] bool alive(entity ent) const noexcept {
        return _entity_pool.alive(ent);
    }

    /// @brief Get reference to component C
    ///
    /// @tparam C Component C
    /// @param ent Entity to read componet from
    /// @return C& Reference to component C
    template<component C>
    [[nodiscard]] C& get(entity ent) {
        return std::get<0>(get_impl<C&>(*this, ent));
    }

    /// @brief Get const reference to component C
    ///
    /// @tparam C Component C
    /// @param ent Entity to read componet from
    /// @return const C& Const reference to component C
    template<component C>
    [[nodiscard]] const C& get(entity ent) const {
        return std::get<0>(get_impl<const C&>(*this, ent));
    }

    /// @brief Get components for a single entity
    ///
    /// @tparam Args Component reference
    /// @param ent Entity to query
    /// @return value_type Components tuple
    template<component_reference... Args>
    [[nodiscard]] std::tuple<Args...> get(entity ent) requires(const_component_references_v<Args...>) {
        return get_impl<Args...>(*this, ent);
    }

    /// @brief Get components for a single entity
    ///
    /// @tparam Args Component reference
    /// @param ent Entity to query
    /// @return value_type Components tuple
    template<component_reference... Args>
    [[nodiscard]] std::tuple<Args...> get(entity ent) const requires(!const_component_references_v<Args...>) {
        return get_impl<Args...>(*this, ent);
    }

    /// @brief Check if entity has component attached or not
    ///
    /// @tparam C Compone ttype
    /// @param ent Entity to check
    /// @return true If entity has component C attached
    /// @return false Otherwise
    template<component C>
    [[nodiscard]] bool has(entity ent) const {
        ensure_alive(ent);
        auto entity_id = ent.id();
        const auto& location = get_location(entity_id);
        return location.archetype->template contains<C>();
    }

    /// @brief Get the resource object
    ///
    /// @tparam R Resource type
    /// @return R& Reference to the resource
    template<resource R>
    R& get_resource() {
        try {
            return _resources.at(resource_family::id<R>).template get<R>();
        } catch (const std::out_of_range&) {
            throw resource_not_found{ type_meta::of<R>() };
        }
    }

    /// @brief Get the resource object
    ///
    /// @tparam R Resource type
    /// @return R& Reference to the resource
    template<resource R>
    const R& get_resource() const {
        try {
            return _resources.at(resource_family::id<R>).template get<R>();
        } catch (const std::out_of_range&) {
            throw resource_not_found{ type_meta::of<R>() };
        }
    }

    /// @brief Set the resource object
    ///
    /// @tparam R Resource type
    /// @tparam Args Arguments type pack
    /// @param args Arguments to construct resource from
    template<resource R, typename... Args>
    R& set_resource(Args&&... args) {
        auto id = resource_family::id<R>;
        auto iter = _resources.find(id);
        if (iter == _resources.end()) {
            auto [emplaced_iter, _] =
                _resources.emplace(id, resource_container::create<R>(std::forward<Args>(args)...));
            iter = emplaced_iter;
        }
        return iter->second.template get<R>();
    }

    /// @brief Remove resource from the registry
    ///
    /// @tparam R Resource type
    template<resource R>
    void remove_resource() {
        _resources.erase(resource_family::id<R>);
    }

    /// @brief Get the event queue object
    ///
    /// @tparam T Event type
    /// @return event_vector<T>& Event vector
    template<event T>
    event_vector<T>& get_event_queue() {
        auto [iter, _] = _event_queues.emplace(event_family::id<T>, event_storage::create<T>());
        return iter->second.template get<T>();
    }

    /// @brief Get the event queue object
    ///
    /// @tparam T Event type
    /// @return const event_vector& Event vector
    template<event T>
    const event_vector<T>& get_event_queue() const {
        auto [iter, _] = _event_queues.emplace(event_family::id<T>, event_storage::create<T>());
        return iter->second.template get<T>();
    }

    /// @brief Flush event queues
    void flush_event_queues() {
        for (auto& [_, queue] : _event_queues) {
            queue.clear();
        }
    }

    /// @brief Get the archetypes object
    ///
    /// @return archetypes&
    [[nodiscard]] archetypes& get_archetypes() noexcept {
        return _archetypes;
    }

    /// @brief Get the archetypes object
    ///
    /// @return const archetypes&
    [[nodiscard]] const archetypes& get_archetypes() const noexcept {
        return _archetypes;
    }

private:
    template<component_reference... Args>
    static std::tuple<Args...> get_impl(auto&& self, entity ent) {
        self.ensure_alive(ent);
        auto& location = self.get_location(ent.id());
        auto* archetype = location.archetype;
        return std::tuple<Args...>(std::ref(archetype->template get<Args>(location))...);
    }

    inline void ensure_alive(const entity& ent) const {
        if (!alive(ent)) {
            throw entity_not_found{ ent };
        }
    }

    [[nodiscard]] const entity_location& get_location(entity_id entity_id) const {
        return _entity_archetype_map.at(entity_id);
    }

    [[nodiscard]] entity_location& get_location(entity_id id) {
        return _entity_archetype_map.at(id);
    }

    void set_location(entity_id entity_id, const entity_location& location) {
        _entity_archetype_map[entity_id] = location;
    }

    void remove_location(entity_id entity_id) {
        _entity_archetype_map.erase(entity_id);
    }

    entity_pool _entity_pool;
    archetypes _archetypes;
    asl::sparse_map<entity_id, entity_location> _entity_archetype_map;
    asl::sparse_map<resource_id, resource_container> _resources;
    mutable asl::sparse_map<event_id, event_storage> _event_queues;
};

} // namespace cobalt::ecs