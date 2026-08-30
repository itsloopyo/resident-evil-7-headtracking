#include "pch.h"
#include "gui_compensation.h"
#include "game_state_detector.h"

#include <cameraunlock/reframework/camera_pipeline.h>
#include <cameraunlock/reframework/gui_elements.h>
#include <cameraunlock/reframework/log_callback.h>
#include <cameraunlock/reframework/plugin_mod.h>

#include <reframework/API.hpp>

#include <cstring>

namespace RE7HT {

namespace ref = cameraunlock::reframework;

// Safe by construction: reads ONLY through managed invokes on `element`
// (get_GameObject -> get_Name -> get_View, set_Position). It never touches the
// raw `context` arg - that raw fixed-offset dereference is what crashed RE7,
// whose GUI struct layout differs from Requiem's.

// RE7's title / main-menu / loading GUI elements. They render over a live 3D
// backdrop that otherwise passes every gameplay tier, so their presence is the
// one reliable "not gameplay" signal. Names captured from the discovery log.
static bool IsMenuElement(const char* goName) {
    return strcmp(goName, "TitleScreen") == 0
        || strcmp(goName, "TitleFlow01_PC") == 0
        || strcmp(goName, "TitleMovie") == 0
        || strcmp(goName, "GUI_menu") == 0
        || strcmp(goName, "NowLoadingScreen") == 0
        || strcmp(goName, "LogoMovie") == 0
        || strcmp(goName, "ToVillageGUI") == 0;
}

// RE7's world-anchored HUD markers. The interaction prompt has two distance
// forms the engine swaps between: "InteractPointGuide_FarIcon" (the small
// ranged chevron) and "InteractPointGuide" (the close-up button+label prompt).
// "GuideIcon" is the objective marker. All anchor to world points and drift
// across the screen as the head rotates unless compensated. Names captured from
// the discovery log.
static bool IsWorldMarker(const char* goName) {
    return strcmp(goName, "InteractPointGuide_FarIcon") == 0
        || strcmp(goName, "InteractPointGuide") == 0
        || strcmp(goName, "GuideIcon") == 0;
}

// Shift a world-anchored marker's root View to the head-tracked screen position
// of its clean-view world target, gluing it back onto the subject. The
// projection is rotation-only; see FrameProjection for why no lean term.
static void ApplyMarkerCompensation(reframework::API::ManagedObject* guiMo) {
    const auto& projection = ref::GetFrameProjection();
    if (!projection.markerValid || !IsInGameplay()) return;

    float fx = 0.f, fy = 0.f;
    if (!ref::GetMarkerFocalLengths(fx, fy)) return;

    float deltaX = -projection.markerTanRight * fx;
    float deltaY =  projection.markerTanUp * fy;

    if (!ref::ShiftElementView(guiMo, deltaX, deltaY)) return;

    // Capped: the 120-frame interval alone streams for the whole
    // session, which buries the startup chain a user is asked to send.
    static int s_markerDiagFrame = 0;
    static int s_markerDiagFrameLeft = 5;
    if (s_markerDiagFrameLeft > 0 && (s_markerDiagFrame++ % 120) == 0) {
        s_markerDiagFrameLeft--;
        ref::LogInfo("Marker comp: fx=%.1f fy=%.1f tanR=%.4f tanU=%.4f delta=(%.1f,%.1f)",
            fx, fy, projection.markerTanRight, projection.markerTanUp, deltaX, deltaY);
    }
}

bool OnPreGuiDrawElement(void* element, void* context) {
    (void)context;  // never read - see note above
    if (!element) return true;
    if (!ref::PluginMod::Instance().IsEnabled()) return true;

    auto* mo = reinterpret_cast<reframework::API::ManagedObject*>(element);

    char name[128] = {};
    if (!ref::ReadGuiElementName(mo, name, sizeof(name))) return true;
    ref::LogGuiElementNameOnce(name);

    if (IsMenuElement(name)) {
        NotifyMainMenuDrawn();
    } else if (IsWorldMarker(name)) {
        ApplyMarkerCompensation(mo);
    }

    return true;
}

} // namespace RE7HT
