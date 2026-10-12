#pragma once
// Smooth Motion (native only, opt-in: PETARI_SMOOTH_MOTION=1, for 120 Hz screens).
//
// The game still steps at 60 Hz. GameSystem::frameLoop draws each step twice:
// first halfway between the previous step and this one, then the step itself.
// The in-between frame re-runs only the 3D view stage (J3DModel::viewCalc) with
// the camera view blended and, per model, the base transform blended; joint
// animation, particle positions and 2D layouts show the step's own state.
// Camera cuts, teleports and models without a previous pose are not blended.
#ifdef PETARI_NATIVE

namespace SmoothMotion {
bool enabled();
// GameSystem::frameLoop: a new game step begins.
void beginStep();
// GameScene::calcAnim ran its 3D view stage this step (blending applies only then).
void noteViewStage();
// After calcAnim: blends the camera and re-runs the 3D view stage halfway back.
// True if the caller should draw and present an in-between frame now.
bool beginBetween();
// After the in-between frame is presented: restores the step's own view.
void endBetween();
// GameSystem::frameLoop, before the retrace wait: the step's busy time, which
// decides whether the next in-between frame fits in the 60 Hz budget.
void endStep();
}  // namespace SmoothMotion

#endif
