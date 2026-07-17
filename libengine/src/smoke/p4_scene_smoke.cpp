/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** P4 scene smoke — exercises the lpl::scene graph (Fixed32 affine transforms,
** parent/child world composition, undo/redo, multi-select) inside the kernel.
** The reported raw Fixed32 values must match the Linux oracle
** (tests/test-scene-parity) bit-for-bit — they are Fixed32/CORDIC authority.
*/
#include "libengine/libengine.h"

#include <lpl/scene/Scene.hpp>

extern "C" void libengine_p4_scene_smoke(libengine_p4_scene_smoke_result_t *out)
{
    using namespace lpl;
    using scene::Fixed32;

    if (out == nullptr)
        return;
    *out = libengine_p4_scene_smoke_result_t{};

    scene::Scene s;
    const scene::NodeId root = s.createNode();
    const scene::NodeId child = s.createNode(root);

    s.setLocalTransform(root, scene::Transform2D::translation(Fixed32::fromInt(10), Fixed32::fromInt(20)));
    s.setLocalTransform(child, scene::Transform2D::translation(Fixed32::fromInt(5), Fixed32::fromInt(0)));

    const scene::Transform2D world = s.worldTransform(child);
    out->world_tx_raw = static_cast<core::u32>(world.tx.raw());
    out->world_ty_raw = static_cast<core::u32>(world.ty.raw());

    // Edit -> undo -> redo on the child's translation.
    s.setLocalTransform(child, scene::Transform2D::translation(Fixed32::fromInt(7), Fixed32::fromInt(7)));
    s.undo();
    out->undo_tx_raw = static_cast<core::u32>(s.localTransform(child).tx.raw());
    s.redo();
    out->redo_tx_raw = static_cast<core::u32>(s.localTransform(child).tx.raw());

    s.select(root);
    s.select(child);
    s.select(root); // duplicate ignored
    out->selection = s.selectionCount();

    const Fixed32 halfPi = Fixed32::fromFloat(1.57079632679f);
    const scene::Transform2D rot = scene::Transform2D::fromTRS(Fixed32::fromInt(0), Fixed32::fromInt(0), halfPi,
                                                              Fixed32::fromInt(1), Fixed32::fromInt(1));
    Fixed32 rx{Fixed32::fromInt(0)};
    Fixed32 ry{Fixed32::fromInt(0)};
    rot.apply(Fixed32::fromInt(1), Fixed32::fromInt(0), rx, ry);
    out->rot_x_raw = rx.raw();
    out->rot_y_raw = ry.raw();

    out->scene_ok = (out->world_tx_raw == static_cast<core::u32>(Fixed32::fromInt(15).raw()) &&
                     out->world_ty_raw == static_cast<core::u32>(Fixed32::fromInt(20).raw()) &&
                     out->undo_tx_raw == static_cast<core::u32>(Fixed32::fromInt(5).raw()) &&
                     out->redo_tx_raw == static_cast<core::u32>(Fixed32::fromInt(7).raw()) &&
                     out->selection == 2u && out->rot_x_raw > -512 && out->rot_x_raw < 512 &&
                     out->rot_y_raw > 65024 && out->rot_y_raw < 66048)
                        ? 1u
                        : 0u;
}
