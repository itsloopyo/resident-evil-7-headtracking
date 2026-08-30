#include "pch.h"
#include "gui_compensation.h"
#include "game_state_detector.h"

#include <cameraunlock/math/smoothing_utils.h>
#include <cameraunlock/reframework/camera_pipeline.h>
#include <cameraunlock/reframework/gui_elements.h>
#include <cameraunlock/reframework/log_callback.h>
#include <cameraunlock/reframework/plugin_mod.h>
#include <cameraunlock/reframework/re_math.h>

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

// InteractPointGuide is the close-up button+label prompt; the engine swaps to
// InteractPointGuide_FarIcon beyond its range. Only the close form gets the lean
// term below.
static bool IsNearPromptMarker(const char* goName) {
    return strcmp(goName, "InteractPointGuide") == 0;
}

// Assumed distance to the world object a close-up interaction prompt sits on.
//
// The prompt's screen shift under a lean is lean/depth, so the term needs a
// depth and nothing here can measure one. What makes a constant safe rather than
// a guess is the residual: correcting with an assumed depth d_a leaves
// f*lean*(1/d_true - 1/d_a), which is smaller than the f*lean/d_true the mod
// leaves today for every d_a > d_true/2. So the constant belongs at the top of
// the range the element appears over, and only an element the game itself splits
// into a near and a far form has a top to its range. InteractPointGuide_FarIcon
// and GuideIcon do not, and stay rotation-only, which is the d_a -> infinity
// case and is never worse than doing nothing.
constexpr float kNearPromptDepthMeters = 3.0f;

// Shift a world-anchored marker's root View to the head-tracked screen position
// of its clean-view world target, gluing it back onto the subject.
//
// The post-render callback restores the clean camera in full, position row
// included, so at GUI draw time the game projects the anchor from the un-leaned
// eye while the frame was drawn from the leaned one. Two things differ. Rotation
// is FrameProjection's marker tangents, depth-independent. Translation is
// lean/depth, worth the assumed depth above for a near prompt - a 0.30 m lateral
// lean displaces a 3 m anchor by fy*0.30/3, about 70 px on the reference canvas
// - and left alone for anything whose depth has no bound.
static bool ComputeMarkerDelta(bool nearPrompt, float fx, float fy,
                               float& deltaX, float& deltaY) {
    const auto& projection = ref::GetFrameProjection();

    if (!nearPrompt) {
        if (!projection.markerValid) return false;
        deltaX = -projection.markerTanRight * fx;
        deltaY =  projection.markerTanUp * fy;
        return true;
    }

    if (!projection.cleanToHeadValid) return false;

    // The prompt is drawn over what the player is looking at, so its clean-view
    // ray is taken as the view forward: (0, 0, depth) in clean-camera axes.
    const float* lean = projection.cleanLocalPositionDelta;
    float rawX = 0.f, rawY = 0.f;
    if (!ref::ProjectCleanRayToHeadGui(projection.cleanToHead, 0.f,
                                       -lean[0], -lean[1], kNearPromptDepthMeters - lean[2],
                                       fx, fy, rawX, rawY)) {
        return false;
    }

    static cameraunlock::math::SmoothedFloat s_deltaX;
    static cameraunlock::math::SmoothedFloat s_deltaY;
    const float dt = ref::PluginMod::Instance().GetLastDeltaTime();
    deltaX = s_deltaX.Update(rawX, ref::kProjectionSmoothing, dt);
    deltaY = s_deltaY.Update(rawY, ref::kProjectionSmoothing, dt);
    return true;
}

static void ApplyMarkerCompensation(reframework::API::ManagedObject* guiMo, bool nearPrompt) {
    if (!IsInGameplay()) return;

    float fx = 0.f, fy = 0.f;
    if (!ref::GetMarkerFocalLengths(fx, fy)) return;

    float deltaX = 0.f, deltaY = 0.f;
    if (!ComputeMarkerDelta(nearPrompt, fx, fy, deltaX, deltaY)) return;

    if (!ref::ShiftElementView(guiMo, deltaX, deltaY)) return;

    // Capped: the 120-frame interval alone streams for the whole
    // session, which buries the startup chain a user is asked to send. The lean
    // triple is the only place the head's translation reaches the log, and it is
    // the quantity the near-prompt branch turns on.
    static int s_markerDiagFrame = 0;
    static int s_markerDiagFrameLeft = 5;
    if (s_markerDiagFrameLeft > 0 && (s_markerDiagFrame++ % 120) == 0) {
        s_markerDiagFrameLeft--;
        const auto& projection = ref::GetFrameProjection();
        ref::LogInfo("Marker comp: near=%d fx=%.1f fy=%.1f tanR=%.4f tanU=%.4f "
            "lean=(%.3f,%.3f,%.3f) delta=(%.1f,%.1f)",
            nearPrompt ? 1 : 0, fx, fy, projection.markerTanRight, projection.markerTanUp,
            projection.cleanLocalPositionDelta[0], projection.cleanLocalPositionDelta[1],
            projection.cleanLocalPositionDelta[2], deltaX, deltaY);
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
        ApplyMarkerCompensation(mo, IsNearPromptMarker(name));
    }

    return true;
}

} // namespace RE7HT
