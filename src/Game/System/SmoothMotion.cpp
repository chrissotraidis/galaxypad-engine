// Smooth Motion: see Game/System/SmoothMotion.hpp.
#ifdef PETARI_NATIVE

#include "Game/System/SmoothMotion.hpp"
#include "Game/Camera/CameraContext.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include <JSystem/J3DGraphAnimator/J3DModel.hpp>
#include <JSystem/J3DGraphBase/J3DSys.hpp>
#include <petari/host_allocation.hpp>
#include <petari/app.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <unordered_map>

// J3DModel.cpp: called at the start of every J3DModel::viewCalc.
extern bool (*gPetariJ3DViewAdjust)(J3DModel* pModel, MtxPtr pView);
// native/input/mods.cpp; Mods::Mod::SmoothMotion.
extern "C" bool petari_mod_enabled(int mod);

namespace {

constexpr int kSmoothMotionMod = 3;
constexpr int kMinimumDisplayHz = 110;

struct Pose {
    Mtx prev;
    Mtx cur;
    unsigned long prevStep = 0;
    unsigned long curStep = 0;
};

constexpr float kModelTeleport = 1500.0f;   // units moved in one step
constexpr float kCameraTeleport = 3000.0f;
constexpr float kCameraCutCos = 0.8660254f;  // 30 degrees of turn in one step

std::unordered_map< const J3DModel*, Pose > gModels;
Pose gCamera;
unsigned long gStep = 1;
bool gActive = false;  // this step
bool gViewStage = false;
bool gBetween = false;
TPos3f gSavedView, gSavedInv;
CameraContext* gContext = nullptr;
// The frame loop's thread; scene loading also builds models (and runs viewCalc)
// on another game thread, which Smooth Motion leaves alone.
std::thread::id gFrameThread;

// Report: steps, in-between frames and wall time, every 600 steps.
unsigned long gReportSteps = 0, gReportBetween = 0;
std::chrono::steady_clock::time_point gReportStart;

void record(Pose& rPose, const Mtx m) {
    if (rPose.curStep != gStep) {
        PSMTXCopy(rPose.cur, rPose.prev);
        rPose.prevStep = rPose.curStep;
        rPose.curStep = gStep;
    }
    PSMTXCopy(m, rPose.cur);
}

float distance(const Mtx a, const Mtx b) {
    const float dx = a[0][3] - b[0][3], dy = a[1][3] - b[1][3], dz = a[2][3] - b[2][3];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// Halfway between the previous and this step's pose, if both exist.
bool halfway(const Pose& rPose, float teleport, Mtx out) {
    if (rPose.curStep != gStep || rPose.prevStep + 1 != gStep) {
        return false;
    }
    if (distance(rPose.prev, rPose.cur) > teleport) {
        return false;
    }
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 4; j++) {
            out[i][j] = 0.5f * (rPose.prev[i][j] + rPose.cur[i][j]);
        }
    }
    return true;
}

bool adjustModel(J3DModel* pModel, MtxPtr pView) {
    if (std::this_thread::get_id() != gFrameThread) {
        return false;
    }
    // Diagnostic: PETARI_SMOOTH_MOTION_MODELS=0 blends the camera only.
    static const bool models = [] {
        const char* value = std::getenv("PETARI_SMOOTH_MOTION_MODELS");
        return value == nullptr || value[0] != '0';
    }();
    if (!models) {
        return false;
    }
    // The table's nodes come from the host heap, not the scene heap freed at a scene change.
    PetariNative::HostAllocationScope host;
    Pose& rPose = gModels[pModel];
    if (!gBetween) {
        record(rPose, pModel->mBaseTransformMtx);
        return false;
    }
    Mtx mid, inverse, delta;
    if (!halfway(rPose, kModelTeleport, mid) || !PSMTXInverse(rPose.cur, inverse)) {
        return false;
    }
    // view' = view * mid * cur^-1: the model's whole skeleton moves with its base.
    PSMTXConcat(mid, inverse, delta);
    PSMTXConcat(pView, delta, pView);
    return true;
}

void prune() {
    PetariNative::HostAllocationScope host;
    for (auto it = gModels.begin(); it != gModels.end();) {
        it = it->second.curStep + 2 < gStep ? gModels.erase(it) : std::next(it);
    }
}

void report() {
    if (gReportSteps == 0) {
        gReportStart = std::chrono::steady_clock::now();
    }
    if (++gReportSteps < 600) {
        return;
    }
    const double seconds = std::chrono::duration< double >(std::chrono::steady_clock::now() - gReportStart).count();
    std::fprintf(stderr, "Petari Smooth Motion: %lu steps, %lu in-between frames in %.2f s: %.1f steps/s, %.1f presents/s\n",
                 gReportSteps, gReportBetween, seconds, gReportSteps / seconds, (gReportSteps + gReportBetween) / seconds);
    gReportSteps = 0;
    gReportBetween = 0;
}

void reView() {
    MR::loadViewMtx();
    CategoryList::entryDrawBuffer3D();
}

}  // namespace

namespace SmoothMotion {

// PETARI_SMOOTH_MOTION=1 or 0 decides; otherwise the SmoothMotion mod (Home menu,
// Mods; mods.txt), on displays of 110 Hz or more. Two presents per step on a 60 Hz
// display would halve the game's speed.
bool enabled() {
    static const int forced = [] {
        const char* value = std::getenv("PETARI_SMOOTH_MOTION");
        return value == nullptr ? -1 : value[0] == '1' ? 1 : 0;
    }();
    if (forced >= 0) {
        return forced == 1;
    }
    return petari_mod_enabled(kSmoothMotionMod) && petari_host_display_hz() >= kMinimumDisplayHz;
}

void beginStep() {
    const bool active = enabled();
    if (active != gActive) {
        gActive = active;
        gPetariJ3DViewAdjust = active ? adjustModel : nullptr;
        gReportSteps = 0;
        gReportBetween = 0;
        std::fprintf(stderr, "Petari Smooth Motion: %s\n", active ? "on (each 60 Hz step is drawn twice)" : "off");
    }
    if (!active) {
        return;
    }
    report();
    gFrameThread = std::this_thread::get_id();
    gStep++;
    gViewStage = false;
    if ((gStep & 255) == 0) {
        prune();
    }
}

void noteViewStage() {
    gViewStage = true;
}

bool beginBetween() {
    if (!gActive || !gViewStage || !MR::isExistSceneObj(SceneObj_CameraContext)) {
        return false;
    }
    gContext = MR::getSceneObj< CameraContext >(SceneObj_CameraContext);
    record(gCamera, gContext->mView.mMtx);
    Mtx mid;
    if (!halfway(gCamera, 1e30f, mid)) {
        return false;
    }
    // Cuts: a large move of the eye or a large turn is shown as is.
    TPos3f prevInv, curInv;
    PSMTXInverse(gCamera.prev, prevInv.mMtx);
    PSMTXInverse(gCamera.cur, curInv.mMtx);
    const float forward = prevInv.mMtx[0][2] * curInv.mMtx[0][2] + prevInv.mMtx[1][2] * curInv.mMtx[1][2] +
                          prevInv.mMtx[2][2] * curInv.mMtx[2][2];
    if (distance(prevInv.mMtx, curInv.mMtx) > kCameraTeleport || forward < kCameraCutCos) {
        return false;
    }
    gSavedView = gContext->mView;
    gSavedInv = gContext->mViewInv;
    PSMTXCopy(mid, gContext->mView.mMtx);
    PSMTXInverse(mid, gContext->mViewInv.mMtx);
    gBetween = true;
    reView();
    gReportBetween++;
    return true;
}

void endBetween() {
    if (!gBetween) {
        return;
    }
    gContext->mView = gSavedView;
    gContext->mViewInv = gSavedInv;
    gBetween = false;
    reView();
}

}  // namespace SmoothMotion

#endif
