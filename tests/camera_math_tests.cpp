// Camera-math invariants that the head-tracking hot path depends on.
//
// camera_hook.cpp skips the per-frame rotation block when yaw/pitch/roll are
// all exactly zero, on the premise that building the rotation from zero angles
// yields the exact identity transform and that left-multiplying a world matrix
// by the identity returns it bit-for-bit. If that premise ever breaks (e.g. a
// change to the shared quaternion math), the skip would stop being
// byte-identical. These tests pin the premise.

#include "pch.h"

#include <cameraunlock/reframework/re_math.h>

#include <cmath>
#include <iostream>

namespace {

using cameraunlock::reframework::Matrix4x4f;
using cameraunlock::reframework::REQuat;
using cameraunlock::reframework::QuatMul;
using cameraunlock::reframework::QuatNorm;
using cameraunlock::reframework::QuatToMatrix3x3;

int g_failures = 0;

void Check(bool cond, const char* name) {
    if (cond) {
        std::cout << "  [PASS] " << name << "\n";
    } else {
        std::cout << "  [FAIL] " << name << "\n";
        ++g_failures;
    }
}

// Mirror of camera_hook.cpp PreMultiplyRotation3x3 (a static there).
void PreMultiplyRotation3x3(Matrix4x4f* worldMat, const float rot[3][3]) {
    for (int c = 0; c < 3; c++) {
        float c0 = worldMat->m[0][c];
        float c1 = worldMat->m[1][c];
        float c2 = worldMat->m[2][c];
        worldMat->m[0][c] = rot[0][0]*c0 + rot[0][1]*c1 + rot[0][2]*c2;
        worldMat->m[1][c] = rot[1][0]*c0 + rot[1][1]*c1 + rot[1][2]*c2;
        worldMat->m[2][c] = rot[2][0]*c0 + rot[2][1]*c1 + rot[2][2]*c2;
    }
}

bool MatrixBitEqual(const Matrix4x4f& a, const Matrix4x4f& b) {
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            if (a.m[i][j] != b.m[i][j]) return false;
    return true;
}

}  // namespace

int RunCameraMathTests() {
    std::cout << "Camera math tests\n";

    // A representative non-trivial world matrix (rotation basis + translation).
    const Matrix4x4f sample = {{
        { 0.36f, -0.48f,  0.80f, 0.0f},
        { 0.80f,  0.60f,  0.00f, 0.0f},
        {-0.48f,  0.64f,  0.60f, 0.0f},
        { 1.50f,  2.25f, -3.75f, 1.0f},
    }};

    // Premise 1: the identity quaternion maps to the exact identity 3x3.
    {
        REQuat id = QuatNorm(QuatMul(REQuat{0,0,0,1}, REQuat{0,0,0,1}));
        float m[3][3];
        QuatToMatrix3x3(id, m);
        bool exact = true;
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                if (m[i][j] != ((i == j) ? 1.0f : 0.0f)) exact = false;
        Check(exact, "identity quaternion -> exact identity 3x3");
    }

    // Premise 2: building the camera-local rotation from zero yaw/pitch/roll
    // yields the exact identity, and pre-multiplying the sample by it is a
    // byte-identical no-op (this is what the hot-path skip relies on).
    {
        float yr = 0.0f, pr = 0.0f, rr = 0.0f;
        float hy = yr*0.5f, hp = pr*0.5f, hr = rr*0.5f;
        REQuat qy = {0, sinf(hy), 0, cosf(hy)};
        REQuat qx = {sinf(hp), 0, 0, cosf(hp)};
        REQuat qz = {0, 0, sinf(hr), cosf(hr)};
        REQuat q = QuatNorm(QuatMul(QuatMul(qy, qx), qz));
        float headRot[3][3];
        QuatToMatrix3x3(q, headRot);

        Matrix4x4f applied = sample;
        PreMultiplyRotation3x3(&applied, headRot);
        Check(MatrixBitEqual(applied, sample),
              "zero camera-local rotation is a byte-identical no-op");
    }

    // Premise 3: the world-space yaw=0 in-place shear uses cy=1, sy=0 and so
    // leaves the matrix untouched.
    {
        float yr = 0.0f;
        float cy = cosf(yr), sy = -sinf(yr);
        Matrix4x4f applied = sample;
        for (int r = 0; r < 3; r++) {
            float x = applied.m[r][0];
            float z = applied.m[r][2];
            applied.m[r][0] = x * cy - z * sy;
            applied.m[r][2] = x * sy + z * cy;
        }
        Check(MatrixBitEqual(applied, sample),
              "zero world-space yaw is a byte-identical no-op");
    }

    return g_failures;
}
