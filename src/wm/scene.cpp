#include "internal.hpp"

void wm_init_scene(WmServer* wm)
{
    wm->scene = scene_create(wm->gpu);
    for (auto layer : enum_values<WmLayer>()) {
        auto* tree = (wm->layers[layer] = scene_tree_create()).get();
        scene_tree_place_above(scene_get_root(wm->scene.get()), nullptr, tree);
    }

    scene_add_damage_listener(wm->scene.get(), [wm](SceneNode* node) {
        for (auto* output : wm->io.outputs) {
            output->interface.request_frame(output->userdata);
        }

        if (dynamic_cast<SceneInputRegion*>(node)) {
            exec_enqueue(wm->exec, [wm = Weak(wm)] {
                if (!wm) return;
                for (auto* seat : wm_get_seats(wm.get())) {
                    if (wm) wm_pointer_move(seat, {}, {});
                }
            });
        }
    });
}
