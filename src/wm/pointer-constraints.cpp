#include "internal.hpp"

#include <core/math.hpp>

void wm_pointer_constraints_init(WmServer* wm)
{
    wm_add_event_filter(wm, [wm](WmEvent* event) -> WmEventFilterResult {
        if (event->type == WmEventType::keyboard_enter || event->type == WmEventType::keyboard_leave) {
            wm_update_active_pointer_constraint(wm);
        }
        return WmEventFilterResult::passthrough;
    });
}

auto wm_constrain_pointer(WmSurface* surface, region2f32 region, WmPointerConstraintType type) -> Ref<WmPointerConstraint>
{
    auto* wm = surface->client->wm;

    auto constraint = ref_create<WmPointerConstraint>();
    constraint->wm = wm;
    constraint->surface = surface;
    constraint->type = type;

    wm->pointer_constraints.insert(wm->pointer_constraints.begin(), constraint.get());

    wm_pointer_constraint_set_region(constraint.get(), region);
    return constraint;
}

void wm_pointer_constraint_set_region(WmPointerConstraint* constraint, region2f32 region)
{
    constraint->region = region;
}

WmPointerConstraint::~WmPointerConstraint()
{
    std::erase(wm->pointer_constraints, this);
    if (wm->active_pointer_constraint == this) {
        wm_update_active_pointer_constraint(wm);
    }
}

auto wm_pointer_constraint_apply(WmServer* wm, vec2f32 position, vec2f32 delta) -> vec2f32
{
    wm_update_active_pointer_constraint(wm);

    if (!wm->active_pointer_constraint) return position + delta;

    auto* input_region = wm->active_pointer_constraint->surface->input_region.get();

    switch (wm->active_pointer_constraint->type) {
        break;case WmPointerConstraintType::locked:
            ;
        break;case WmPointerConstraintType::confined:
            position += delta;
    }

    auto offset = scene_tree_get_position(input_region->parent);

    position -= offset;

    // TODO: Force pointer focus while applying constraint
    rect2f32 clip = input_region->clip;
    clip.extent -= vec2f32(1);

    position = rect_clamp_point(clip, position);
    position = input_region->region.constrain(position);
    position = wm->active_pointer_constraint->region.constrain(position);

    return position + offset;
}

void wm_update_active_pointer_constraint(WmServer* wm)
{
    WmPointerConstraint* new_active = nullptr;
    for (auto* constraint : wm->pointer_constraints) {
        if (!constraint->surface || !constraint->surface->input_region->parent) continue;
        if (!std::ranges::any_of(wm_get_seats(wm), [&](WmSeat* seat) -> bool {
            return wm_surface_contains(constraint->surface.get(), wm_keyboard_get_focus(seat));
        })) continue;

        new_active = constraint;
        break;
    }

    if (wm->active_pointer_constraint == new_active) return;

    if (wm->active_pointer_constraint) {
        for (auto* client : wm->clients) {
            wm_client_post_event(client, ptr_to(WmEvent {
                .pointer = {
                    .type = WmEventType::pointer_constraint_disabled,
                    .constraint = {
                        .constraint = wm->active_pointer_constraint,
                    },
                }
            }));
        }
    }

    wm->active_pointer_constraint = new_active;

    if (new_active) {
        for (auto* client : wm->clients) {
            wm_client_post_event(client, ptr_to(WmEvent {
                .pointer = {
                    .type = WmEventType::pointer_constraint_enabled,
                    .constraint = {
                        .constraint = new_active,
                    }
                }
            }));
        }
    }
}
