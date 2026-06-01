#include "pch.h"
#include "camera_hook.h"
#include "math_types.h"
#include "game_state_detector.h"
#include "core/mod.h"
#include "core/logger.h"

#include <cameraunlock/reframework/managed_utils.h>
#include <cameraunlock/reframework/re_math.h>
#include <cameraunlock/math/smoothing_utils.h>
#include <reframework/API.hpp>

#include <string>

namespace RE7HT {

namespace ref = cameraunlock::reframework;

constexpr int TX_WORLDMATRIX_OFFSET = 0x80;

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

// Method cache for the camera chain
static struct {
    reframework::API::Method* getMainView = nullptr;
    reframework::API::Method* getPrimaryCamera = nullptr;
    reframework::API::Method* getGameObject = nullptr;
    reframework::API::Method* getTransform = nullptr;
    bool initialized = false;
    bool failed = false;
} g_fn;

// Per-frame transform cache
static void* g_cachedTransform = nullptr;

static void* ResolveCameraTransform() {
    const auto& api = reframework::API::get();
    auto sm = api->get_native_singleton("via.SceneManager");
    if (!sm) return nullptr;
    auto mv = ref::CallMethod(g_fn.getMainView, sm);
    if (!mv) return nullptr;
    auto cam = ref::CallMethod(g_fn.getPrimaryCamera, mv);
    if (!cam) return nullptr;
    auto go = ref::CallMethod(g_fn.getGameObject, cam);
    if (!go) return nullptr;
    return ref::CallMethod(g_fn.getTransform, go);
}

static void* GetCameraTransformCached() {
    if (g_cachedTransform) return g_cachedTransform;
    g_cachedTransform = ResolveCameraTransform();
    return g_cachedTransform;
}

// SEH-guarded resolve of the camera transform's world-matrix pointer. The
// resolve walks managed objects that can fault during scene transitions, so it
// stays inside __try. Returns nullptr when the transform can't be resolved.
// Centralizes the TX_WORLDMATRIX_OFFSET application shared by all four hooks.
static Matrix4x4f* GetCameraWorldMatrixGuarded() {
    void* transform = nullptr;
    __try { transform = GetCameraTransformCached(); } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    if (!transform) return nullptr;
    return reinterpret_cast<Matrix4x4f*>(
        reinterpret_cast<uint8_t*>(transform) + TX_WORLDMATRIX_OFFSET);
}

// Left-multiply the upper-left 3x3 of a world matrix (its rotation/scale
// basis) by rot, column by column. The translation row (m[3]) is untouched.
static void PreMultiplyRotation3x3(Matrix4x4f* worldMat, const float rot[3][3]) {
    for (int c = 0; c < 3; c++) {
        float c0 = worldMat->m[0][c];
        float c1 = worldMat->m[1][c];
        float c2 = worldMat->m[2][c];
        worldMat->m[0][c] = rot[0][0]*c0 + rot[0][1]*c1 + rot[0][2]*c2;
        worldMat->m[1][c] = rot[1][0]*c0 + rot[1][1]*c1 + rot[1][2]*c2;
        worldMat->m[2][c] = rot[2][0]*c0 + rot[2][1]*c1 + rot[2][2]*c2;
    }
}

// --- Core head tracking application ---

static void ApplyHeadTracking(Matrix4x4f* worldMat) {
    float yaw, pitch, roll;
    // Zero rotation builds an exact-identity matrix (bit-exact: sin(0)=0,
    // cos(0)=1 give the identity quaternion, which QuatToMatrix3x3 maps to the
    // exact identity, and PreMultiplyRotation3x3 by identity returns the input
    // unchanged). Skipping the rotation block in that case is byte-identical
    // and avoids the per-frame trig/quaternion work in position-only mode and
    // whenever the view is perfectly centered.
    bool hasRotation = Mod::Instance().GetProcessedRotation(yaw, pitch, roll)
                       && (yaw != 0.0f || pitch != 0.0f || roll != 0.0f);

    float px, py, pz;
    bool hasPosition = Mod::Instance().GetPositionOffset(px, py, pz);

    if (!hasRotation && !hasPosition) return;

    // Pre-rotation axes are only read by the position offset below; capture
    // them only when that branch will run.
    Matrix4x4f preRotationAxes;
    if (hasPosition) preRotationAxes = *worldMat;

    // --- Rotation ---
    if (hasRotation) {
        float yr = -yaw * DEG_TO_RAD;
        float pr = pitch * DEG_TO_RAD;
        float rr = roll * DEG_TO_RAD;

        if (Mod::Instance().IsWorldSpaceYaw()) {
            // Horizon-locked yaw: M'' = R_pitchroll * M * R_yaw
            float cy = cosf(yr), sy = -sinf(yr);
            for (int r = 0; r < 3; r++) {
                float x = worldMat->m[r][0];
                float z = worldMat->m[r][2];
                worldMat->m[r][0] = x * cy - z * sy;
                worldMat->m[r][2] = x * sy + z * cy;
            }

            float hp = pr * 0.5f, hr = rr * 0.5f;
            REQuat qx = {sinf(hp), 0, 0, cosf(hp)};
            REQuat qz = {0, 0, sinf(hr), cosf(hr)};
            REQuat qPR = QuatNorm(QuatMul(qx, qz));
            float prRot[3][3];
            QuatToMatrix3x3(qPR, prRot);

            PreMultiplyRotation3x3(worldMat, prRot);
        } else {
            // Camera-local: all axes relative to camera orientation
            float hy = yr * 0.5f, hp = pr * 0.5f, hr = rr * 0.5f;
            REQuat qy = {0, sinf(hy), 0, cosf(hy)};
            REQuat qx = {sinf(hp), 0, 0, cosf(hp)};
            REQuat qz = {0, 0, sinf(hr), cosf(hr)};
            REQuat q = QuatNorm(QuatMul(QuatMul(qy, qx), qz));

            float headRot[3][3];
            QuatToMatrix3x3(q, headRot);

            PreMultiplyRotation3x3(worldMat, headRot);
        }
    }

    // --- Position (6DOF) ---
    if (hasPosition) {
        px = -px;
        const Matrix4x4f& gm = preRotationAxes;
        worldMat->m[3][0] += px * gm.m[0][0] + py * gm.m[1][0] + pz * gm.m[2][0];
        worldMat->m[3][1] += px * gm.m[0][1] + py * gm.m[1][1] + pz * gm.m[2][1];
        worldMat->m[3][2] += px * gm.m[0][2] + py * gm.m[1][2] + pz * gm.m[2][2];
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

static void TryHookCameraController() {
    const auto& api = reframework::API::get();
    auto tdb = api->tdb();

    static const char* controllerTypes[] = {
        "app.PlayerCamera",
        "app.camera.PlayerCamera",
        "app.PlayerCameraController",
        "app.camera.PlayerCameraController",
        "app.CameraManager",
        "app.camera.CameraManager",
    };

    static const char* methodNames[] = {
        "lateUpdate",
        "onCameraUpdate",
        "update",
    };

    for (auto typeName : controllerTypes) {
        auto type = tdb->find_type(typeName);
        if (!type) continue;
        for (auto methodName : methodNames) {
            auto method = type->find_method(methodName);
            if (!method) continue;
            auto id = method->add_hook(CameraUpdatePreHook, CameraUpdatePostHook, false);
            Logger::Instance().Info("Hooked %s.%s (id=%u)", typeName, methodName, id);
            return;
        }
    }

    // Walk parent chain to discover camera controller dynamically
    Logger::Instance().Info("Camera controller: hardcoded names failed, walking parent chain...");

    void* camTransform = ResolveCameraTransform();
    if (camTransform) {
        auto txMo = reinterpret_cast<reframework::API::ManagedObject*>(camTransform);
        for (int depth = 0; depth < 8; depth++) {
            auto goRet = txMo->invoke("get_GameObject", ref::EmptyArgs());
            if (goRet.exception_thrown || !goRet.ptr) break;
            auto goMo = reinterpret_cast<reframework::API::ManagedObject*>(goRet.ptr);

            char goName[128] = "?";
            auto nameRet = goMo->invoke("get_Name", ref::EmptyArgs());
            if (!nameRet.exception_thrown && nameRet.ptr) {
                ref::ReadManagedString(nameRet.ptr, goName, sizeof(goName));
            }

            auto compsRet = goMo->invoke("get_Components", ref::EmptyArgs());
            if (compsRet.exception_thrown || !compsRet.ptr) {
                Logger::Instance().Info("  parent[%d] GO=\"%s\": no components", depth, goName);
            } else {
                auto compArr = reinterpret_cast<reframework::API::ManagedObject*>(compsRet.ptr);
                auto lenRet = compArr->invoke("get_Length", ref::EmptyArgs());
                uint32_t compCount = lenRet.exception_thrown ? 0 : lenRet.dword;
                Logger::Instance().Info("  parent[%d] GO=\"%s\": %u components", depth, goName, compCount);

                for (uint32_t i = 0; i < compCount && i < 32; i++) {
                    auto comp = ref::ArrayGetValue(compArr, (int)i);
                    if (!comp) continue;
                    auto compTd = comp->get_type_definition();
                    if (!compTd) continue;
                    const char* cns = compTd->get_namespace();
                    const char* cnm = compTd->get_name();
                    if (!cns) cns = "";
                    if (!cnm) cnm = "?";
                    Logger::Instance().Info("    [%u] %s.%s", i, cns, cnm);

                    if (cnm && strstr(cnm, "Camera") && strstr(cnm, "Controller")) {
                        char fullName[256];
                        snprintf(fullName, sizeof(fullName), "%s.%s", cns, cnm);
                        Logger::Instance().Info("  -> Candidate camera controller: %s", fullName);

                        auto candidateType = tdb->find_type(fullName);
                        if (candidateType) {
                            for (auto mn : methodNames) {
                                auto m = candidateType->find_method(mn);
                                if (!m) continue;
                                auto id = m->add_hook(CameraUpdatePreHook, CameraUpdatePostHook, false);
                                Logger::Instance().Info("  -> Hooked %s.%s (id=%u)", fullName, mn, id);
                                return;
                            }
                        }
                    }
                }
            }

            auto parentRet = txMo->invoke("get_Parent", ref::EmptyArgs());
            if (parentRet.exception_thrown || !parentRet.ptr) break;
            txMo = reinterpret_cast<reframework::API::ManagedObject*>(parentRet.ptr);
        }
    }

    Logger::Instance().Warning("Camera controller hook not found — aim decoupling relies on PostBeginRendering restore");
}

// --- Initialization ---

static bool InitCachedFunctions() {
    if (g_fn.initialized) return !g_fn.failed;
    g_fn.initialized = true;

    const auto& api = reframework::API::get();
    auto tdb = api->tdb();
    auto smType = tdb->find_type("via.SceneManager");
    auto svType = tdb->find_type("via.SceneView");
    auto camType = tdb->find_type("via.Camera");
    auto goType = tdb->find_type("via.GameObject");

    if (!smType || !svType || !camType || !goType) { g_fn.failed = true; return false; }

    g_fn.getMainView = smType->find_method("get_MainView");
    g_fn.getPrimaryCamera = svType->find_method("get_PrimaryCamera");
    g_fn.getGameObject = camType->find_method("get_GameObject");
    g_fn.getTransform = goType->find_method("get_Transform");

    if (!g_fn.getMainView || !g_fn.getPrimaryCamera || !g_fn.getGameObject || !g_fn.getTransform) {
        g_fn.failed = true;
        return false;
    }

    TryHookCameraController();

    Logger::Instance().Info("Methods cached");
    return true;
}

void OnPreBeginRendering() {
    if (!InitCachedFunctions()) return;
    if (!Mod::Instance().IsEnabled()) return;
    if (!IsInGameplay()) return;
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
