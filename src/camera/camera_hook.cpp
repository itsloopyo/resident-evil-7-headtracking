#include "pch.h"
#include "camera_hook.h"
#include "math_types.h"
#include "game_state_detector.h"
#include "core/mod.h"
#include "core/logger.h"

#include <cameraunlock/reframework/camera_chain.h>
#include <cameraunlock/reframework/camera_controller_hook.h>
#include <cameraunlock/time/qpc_clock.h>
#include <cameraunlock/reframework/managed_utils.h>
#include <cameraunlock/math/smoothing_utils.h>
#include <cameraunlock/rendering/gui_marker_compensation.h>
#include <reframework/API.hpp>

#include <iterator>
#include <unordered_set>
#include <cstring>

namespace RE7HT {

namespace ref = cameraunlock::reframework;

// Clean camera matrix saved before head tracking is applied each frame, so
// OnPostBeginRendering can restore the rotation game logic reads.
struct CleanCameraMatrix {
    Matrix4x4f matrix;
    bool valid = false;
};
static CleanCameraMatrix g_cleanCameraMatrix;

// Per-frame flag: set true when OnPreBeginRendering applies head tracking.
static bool g_trackingAppliedThisFrame = false;

// Saved game rotation - what the game INTENDED before we modified it
static struct {
    Matrix4x4f gameMatrix;
    bool hasGameMatrix = false;
} g_saved;

// Resolver for the standard RE Engine camera transform chain (cameraunlock-core).
static ref::CameraTransformResolver g_resolver;

// Per-frame transform + camera cache. The camera is needed by GUI marker
// compensation to read the live projection matrix / FOV; resolving it alongside
// the transform avoids a second SceneManager chain walk.
static void* g_cachedTransform = nullptr;
static void* g_cachedCamera = nullptr;

static void* GetCameraTransformCached() {
    if (!g_cachedTransform) g_cachedTransform = g_resolver.ResolveTransform(&g_cachedCamera);
    return g_cachedTransform;
}

// World-anchored GUI marker compensation: the rotation-only screen-space shift
// of the clean view forward under head rotation, smoothed. Read by the GUI draw
// hook to reposition interaction/objective markers so they stay glued to their
// world target while the head turns the view. (OnPostBeginRendering keeps the
// head-tracked position, so translation parallax is already handled by the
// engine; only rotation needs compensating.)
struct MarkerProjection {
    float tanRight = 0.0f;
    float tanUp = 0.0f;
    bool valid = false;
};
static MarkerProjection g_marker;

// GUI compensation methods, resolved once lazily on the first drawn element.
static struct {
    reframework::API::Method* transformSetPosition = nullptr;  // via.gui.TransformObject.set_Position
    reframework::API::Method* getProjectionMatrix = nullptr;   // via.Camera.get_ProjectionMatrix
    bool resolved = false;
} g_guiMethods;

// Resolve the camera transform's world-matrix pointer, reusing the per-frame
// cached transform. The resolver's chain walk is SEH-guarded internally, so a
// camera torn down during a scene transition yields nullptr instead of
// crashing. Centralizes the world-matrix-offset application shared by all
// four hooks.
static Matrix4x4f* GetCameraWorldMatrixGuarded() {
    void* transform = GetCameraTransformCached();
    if (!transform) return nullptr;
    return reinterpret_cast<Matrix4x4f*>(
        reinterpret_cast<uint8_t*>(transform) + ref::kTransformWorldMatrixOffset);
}

// --- Core head tracking application ---

static void ApplyHeadTracking(Matrix4x4f* worldMat) {
    float yaw, pitch, roll;
    // Zero rotation builds an exact-identity matrix (bit-exact: sin(0)=0,
    // cos(0)=1 give the identity quaternion, which maps to the exact identity
    // 3x3, and pre-multiplying by identity returns the input unchanged).
    // Skipping the rotation block in that case is byte-identical and avoids
    // the per-frame trig/quaternion work in position-only mode and whenever
    // the view is perfectly centered.
    bool hasRotation = Mod::Instance().GetProcessedRotation(yaw, pitch, roll)
                       && (yaw != 0.0f || pitch != 0.0f || roll != 0.0f);

    float px, py, pz;
    bool hasPosition = Mod::Instance().GetPositionOffset(px, py, pz);

    if (!hasRotation && !hasPosition) return;

    // Pre-rotation axes are only read by the position offset below; capture
    // them only when that branch will run.
    Matrix4x4f preRotationAxes;
    if (hasPosition) preRotationAxes = *worldMat;

    if (hasRotation) {
        float yr = -yaw * DEG_TO_RAD;
        float pr = pitch * DEG_TO_RAD;
        float rr = roll * DEG_TO_RAD;

        if (Mod::Instance().IsWorldSpaceYaw()) {
            ApplyWorldSpaceHeadRotation(*worldMat, yr, pr, rr);
        } else {
            ApplyCameraLocalHeadRotation(*worldMat, yr, pr, rr);
        }
    }

    if (hasPosition) {
        ApplyViewSpacePositionOffset(*worldMat, preRotationAxes, px, py, pz);
    }
}

// --- Camera controller hooks (save/restore) ---

static int CameraUpdatePreHook(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr) {
    g_cachedTransform = nullptr;
    g_cachedCamera = nullptr;

    if (!g_saved.hasGameMatrix || !Mod::Instance().IsEnabled()) {
        return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    }

    Matrix4x4f* worldMat = GetCameraWorldMatrixGuarded();
    if (!worldMat) return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    __try {
        *worldMat = g_saved.gameMatrix;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

static void CameraUpdatePostHook(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr) {
    Matrix4x4f* worldMat = GetCameraWorldMatrixGuarded();
    if (!worldMat) return;
    __try {
        g_saved.gameMatrix = *worldMat;
        g_saved.hasGameMatrix = true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    static bool s_loggedOnce = false;
    if (!s_loggedOnce) {
        REQuat q = MatrixToQuat(g_saved.gameMatrix);
        Logger::Instance().Info("Hook save/restore active: gameQ=%.3f %.3f %.3f %.3f", q.x, q.y, q.z, q.w);
        s_loggedOnce = true;
    }
}

// --- Camera controller discovery ---

// Fast-path candidates; app.PlayerCamera.lateUpdate is the confirmed hook
// point on RE7. The hooker's parent-chain walk discovers the real controller
// dynamically and logs the component tree if none of these match.
static const char* const kControllerTypeCandidates[] = {
    "app.PlayerCamera",
    "app.camera.PlayerCamera",
    "app.PlayerCameraController",
    "app.camera.PlayerCameraController",
    "app.CameraManager",
    "app.camera.CameraManager",
};

static ref::CameraControllerHooker g_controllerHooker{
    kControllerTypeCandidates,
    static_cast<int>(std::size(kControllerTypeCandidates)),
    CameraUpdatePreHook,
    CameraUpdatePostHook};

// Minimum gap between repeats of the camera-controller-not-found warning.
constexpr uint64_t kHookWarnIntervalUs = 30ull * 1000000ull;

// Run camera-controller discovery once we are in gameplay, retrying each
// frame until it succeeds. Deferring past the menu avoids latching onto a
// render effect controller before the gameplay camera rig exists.
static void EnsureCameraControllerHooked() {
    if (g_controllerHooker.IsHooked()) return;
    if (g_controllerHooker.TryHook(GetCameraTransformCached())) return;

    // Wall-clock, not frame-count: a frame-gated warning writes hundreds of
    // lines an hour on a high-refresh display and buries the startup sequence.
    int attempts = g_controllerHooker.AttemptCount();
    uint64_t now = cameraunlock::time::QpcNowMicros();
    static uint64_t s_lastHookWarnUs = 0;
    if (attempts == 1 || (now - s_lastHookWarnUs) >= kHookWarnIntervalUs) {
        s_lastHookWarnUs = now;
        Logger::Instance().Warning(
            "Camera controller hook not yet found (attempt %d) - head tracking "
            "still active via the BeginRendering restore path", attempts);
    }
}

// --- Initialization ---

static bool InitCachedFunctions() {
    return g_resolver.Initialize();
}

// Compute the rotation-only screen-space tangent shift of the clean view
// forward under head rotation, smoothed with the internal projection constant.
// head is the head-tracked world matrix; the clean matrix is read from
// g_cleanCameraMatrix.
static void UpdateMarkerProjection(const Matrix4x4f& head) {
    float rawTanRight = 0.f, rawTanUp = 0.f;
    if (ref::ProjectForwardToViewTangents(g_cleanCameraMatrix.matrix, head, rawTanRight, rawTanUp)) {
        float dt = Mod::Instance().GetLastDeltaTime();
        // Internal projection-smoothing constant, deliberately independent of the user's tracking smoothing.
        constexpr float kSmoothing = 0.15f;

        static cameraunlock::math::SmoothedFloat s_tanRight;
        static cameraunlock::math::SmoothedFloat s_tanUp;

        g_marker.tanRight = s_tanRight.Update(rawTanRight, kSmoothing, dt);
        g_marker.tanUp = s_tanUp.Update(rawTanUp, kSmoothing, dt);
        g_marker.valid = true;
    } else {
        g_marker.valid = false;
    }
}

void OnPreBeginRendering() {
    // Before every gate below: the first-packet latch has to survive
    // AutoEnable=false, a menu, and a failed function cache, because those are
    // exactly the states a "no head tracking" report is trying to tell apart.
    Mod::Instance().LogFirstTrackerPose();

    // Drain hotkey requests on the render thread so the mode cycle never
    // mutates session state concurrently with the pipeline tick below.
    Mod::Instance().ProcessDeferredActions();

    if (!InitCachedFunctions()) return;
    if (!Mod::Instance().IsEnabled()) return;
    if (!IsInGameplay()) return;
    EnsureCameraControllerHooked();

    // Advance interpolation + smoothing once per render frame so the
    // rendered camera and the smoother see the same wall-clock dt.
    Mod::Instance().TickFrame();

    Matrix4x4f* worldMat = GetCameraWorldMatrixGuarded();
    if (!worldMat) return;

    // Save the clean matrix
    g_cleanCameraMatrix.matrix = *worldMat;
    g_cleanCameraMatrix.valid = true;

    ApplyHeadTracking(worldMat);
    g_trackingAppliedThisFrame = true;

    UpdateMarkerProjection(*worldMat);
}

void OnPostBeginRendering() {
    if (!g_trackingAppliedThisFrame) return;
    g_trackingAppliedThisFrame = false;

    if (!g_cleanCameraMatrix.valid) return;

    // Reuse the transform OnPreBeginRendering already resolved this frame.
    // g_trackingAppliedThisFrame is only set after that resolve succeeded, and
    // nothing clears the cache between the two BeginRendering callbacks (both
    // run on the render thread; the only other clear sites are the camera
    // lateUpdate pre-hook earlier in the frame and the tail of this function).
    Matrix4x4f* worldMat = GetCameraWorldMatrixGuarded();
    if (!worldMat) return;
    __try {
        // Restore clean ROTATION but keep head-tracked POSITION.
        Matrix4x4f restored = g_cleanCameraMatrix.matrix;
        restored.m[3][0] = worldMat->m[3][0];
        restored.m[3][1] = worldMat->m[3][1];
        restored.m[3][2] = worldMat->m[3][2];
        *worldMat = restored;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    g_cachedTransform = nullptr;
    g_cachedCamera = nullptr;
}

// --- GUI marker compensation + main-menu signal ---
//
// Safe by construction: reads ONLY through managed invokes on `element`
// (get_GameObject -> get_Name -> get_View, set_Position). It never touches the
// raw `context` arg - that raw fixed-offset dereference (RE9's TryDumpContext)
// is what crashed RE7, whose GUI struct layout differs. Modelled on RE Village,
// which solved the same menu-detection and marker-drift problems on the RE7
// sibling engine.

// Per-element instance getters, resolved once from the first element's type
// (find_method walks base types, so the descriptors dispatch on every element).
static struct {
    reframework::API::Method* elemGetGameObject = nullptr;  // <element>.get_GameObject
    reframework::API::Method* getGameObjectName = nullptr;  // via.GameObject.get_Name
    reframework::API::Method* elemGetView = nullptr;        // <element>.get_View
    bool resolved = false;
} g_guiElem;

static void ResolveGuiElementMethods(reframework::API::ManagedObject* element) {
    if (g_guiElem.resolved) return;
    auto td = element->get_type_definition();
    if (!td) return;
    g_guiElem.elemGetGameObject = td->find_method("get_GameObject");
    g_guiElem.elemGetView = td->find_method("get_View");
    auto tdb = reframework::API::get()->tdb();
    auto goType = tdb->find_type("via.GameObject");
    if (goType) g_guiElem.getGameObjectName = goType->find_method("get_Name");
    if (g_guiElem.elemGetGameObject && g_guiElem.getGameObjectName && g_guiElem.elemGetView) {
        g_guiElem.resolved = true;
    }
}

static void InitGUICompensationMethods() {
    if (g_guiMethods.resolved) return;
    g_guiMethods.resolved = true;
    g_guiMethods.transformSetPosition =
        ref::FindMethodByParamCount("via.gui.TransformObject", "set_Position", 1);
    auto tdb = reframework::API::get()->tdb();
    auto camType = tdb ? tdb->find_type("via.Camera") : nullptr;
    g_guiMethods.getProjectionMatrix = camType ? camType->find_method("get_ProjectionMatrix") : nullptr;
    Logger::Instance().Info("GUI compensation methods: setPos=%p projMat=%p",
        (void*)g_guiMethods.transformSetPosition, (void*)g_guiMethods.getProjectionMatrix);
}

// Pixel focal lengths for marker compensation. Prefer the camera's projection
// matrix (P00/P11 give the exact per-axis scale, no FOV-convention guess);
// fall back to deriving from the vertical FOV at a 16:9 square-pixel canvas.
static bool ComputeMarkerFocalLengths(float& fx, float& fy) {
    constexpr float kHalfW = 960.f;
    constexpr float kHalfH = 540.f;

    void* cam = g_cachedCamera ? g_cachedCamera : g_resolver.ResolveCamera();
    if (!cam) return false;

    if (g_guiMethods.getProjectionMatrix) {
        auto ret = g_guiMethods.getProjectionMatrix->invoke(
            reinterpret_cast<reframework::API::ManagedObject*>(cam), ref::EmptyArgs());
        if (!ret.exception_thrown) {
            // Matrix4x4 (64 bytes) returned by value in ret.bytes, row-major.
            auto* m = reinterpret_cast<const float*>(ret.bytes.data());
            if (cameraunlock::rendering::FocalLengthsFromProjection(m[0], m[5], kHalfW, kHalfH, fx, fy)) {
                static bool s_logged = false;
                if (!s_logged) {
                    s_logged = true;
                    Logger::Instance().Info("Marker focal (projection): P00=%.4f P11=%.4f fx=%.1f fy=%.1f",
                        m[0], m[5], fx, fy);
                }
                // Square pixels: horizontal and vertical pixel focal lengths must
                // match. RE7's matrix reports them equal, but the RE3 build proved
                // this projection path can return P00 at half its true value
                // (fx ends up half of fy), which under-compensates yaw and drifts
                // the reticle/markers horizontally. fy (vertical) is the trusted
                // value; enforce fx = fy so a divergent matrix can never slip through.
                fx = fy;
                return true;
            }
        }
    }

    float fov = g_resolver.ResolveFovDegrees(cam);
    return cameraunlock::rendering::FocalLengthsFromVerticalFov(fov, kHalfW, kHalfH, fx, fy);
}

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
// of its clean-view world target, gluing it back onto the subject. See
// UpdateMarkerProjection / OnPostBeginRendering for why only rotation is
// compensated (translation parallax is already handled by the engine).
static void ApplyMarkerCompensation(reframework::API::ManagedObject* guiMo) {
    if (!guiMo || !g_guiMethods.transformSetPosition) return;
    if (!g_marker.valid || !IsInGameplay()) return;

    float fx = 0.f, fy = 0.f;
    if (!ComputeMarkerFocalLengths(fx, fy)) return;

    float deltaX = -g_marker.tanRight * fx;
    float deltaY =  g_marker.tanUp * fy;

    auto viewRet = g_guiElem.elemGetView
        ? g_guiElem.elemGetView->invoke(guiMo, ref::EmptyArgs())
        : guiMo->invoke("get_View", ref::EmptyArgs());
    if (viewRet.exception_thrown || !viewRet.ptr) return;
    auto view = reinterpret_cast<reframework::API::ManagedObject*>(viewRet.ptr);

    float pos[3] = { deltaX, deltaY, 0.f };
    ref::InvokeMethodWithArg(g_guiMethods.transformSetPosition, view, (void*)&pos[0]);

    // Capped: the 120-frame interval alone streams for the whole
    // session, which buries the startup chain a user is asked to send.
    static int s_markerDiagFrame = 0;
    static int s_markerDiagFrameLeft = 5;
    if (s_markerDiagFrameLeft > 0 && (s_markerDiagFrame++ % 120) == 0) {
        s_markerDiagFrameLeft--;
        Logger::Instance().Info("Marker comp: fx=%.1f fy=%.1f tanR=%.4f tanU=%.4f delta=(%.1f,%.1f)",
            fx, fy, g_marker.tanRight, g_marker.tanUp, deltaX, deltaY);
    }
}

bool OnPreGuiDrawElement(void* element, void* context) {
    (void)context;  // never read - see note above
    if (!element) return true;
    if (!Mod::Instance().IsEnabled()) return true;

    auto* mo = reinterpret_cast<reframework::API::ManagedObject*>(element);
    ResolveGuiElementMethods(mo);
    if (!g_guiElem.resolved) return true;
    InitGUICompensationMethods();

    auto goRet = g_guiElem.elemGetGameObject->invoke(mo, ref::EmptyArgs());
    if (goRet.exception_thrown || !goRet.ptr) return true;
    auto* goMo = reinterpret_cast<reframework::API::ManagedObject*>(goRet.ptr);

    auto nameRet = g_guiElem.getGameObjectName->invoke(goMo, ref::EmptyArgs());
    if (nameRet.exception_thrown || !nameRet.ptr) return true;

    char name[128] = {};
    ref::ReadManagedString(nameRet.ptr, name, sizeof(name));
    if (!name[0]) return true;

    static std::unordered_set<std::string> s_logged;
    if (s_logged.size() < 200 && s_logged.insert(std::string(name)).second) {
        Logger::Instance().Info("GUI element: \"%s\"", name);
    }

    if (IsMenuElement(name)) {
        NotifyMainMenuDrawn();
    } else if (IsWorldMarker(name)) {
        ApplyMarkerCompensation(mo);
    }

    return true;
}

} // namespace RE7HT
