#include "pch.h"
#include "camera_hook.h"
#include "math_types.h"
#include "game_state_detector.h"
#include "core/mod.h"
#include "core/logger.h"

#include <cameraunlock/reframework/camera_chain.h>
#include <cameraunlock/reframework/camera_controller_hook.h>
#include <reframework/API.hpp>

#include <iterator>

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

// Saved game rotation — what the game INTENDED before we modified it
static struct {
    Matrix4x4f gameMatrix;
    bool hasGameMatrix = false;
} g_saved;

// Resolver for the standard RE Engine camera transform chain (cameraunlock-core).
static ref::CameraTransformResolver g_resolver;

// Per-frame transform cache
static void* g_cachedTransform = nullptr;

static void* GetCameraTransformCached() {
    if (!g_cachedTransform) g_cachedTransform = g_resolver.ResolveTransform();
    return g_cachedTransform;
}

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

// Run camera-controller discovery once we are in gameplay, retrying each
// frame until it succeeds. Deferring past the menu avoids latching onto a
// render effect controller before the gameplay camera rig exists.
static void EnsureCameraControllerHooked() {
    if (g_controllerHooker.IsHooked()) return;
    if (g_controllerHooker.TryHook(GetCameraTransformCached())) return;

    int attempts = g_controllerHooker.AttemptCount();
    if (attempts == 1 || (attempts % 300) == 0) {
        Logger::Instance().Warning(
            "Camera controller hook not yet found (attempt %d) - head tracking "
            "still active via the BeginRendering restore path", attempts);
    }
}

// --- Initialization ---

static bool InitCachedFunctions() {
    return g_resolver.Initialize();
}

void OnPreBeginRendering() {
    // Drain hotkey requests on the render thread so recenter / mode-cycle
    // never mutate session state concurrently with the pipeline tick below.
    Mod::Instance().ProcessDeferredActions();

    if (!InitCachedFunctions()) return;
    if (!Mod::Instance().IsEnabled()) return;
    if (!IsInGameplay()) return;
    EnsureCameraControllerHooked();
    if (ShouldRecenter()) {
        Mod::Instance().Recenter();
    }

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
}

} // namespace RE7HT
