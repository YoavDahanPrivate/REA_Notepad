#pragma once

// Retro arcade sound effects for the easter-egg games. Every sound is synthesized here from
// square, triangle and noise waves (no recorded samples) and mixed on a small waveOut stream,
// so several sounds can overlap. The mute choice is remembered in the registry.

namespace sfx {

enum Id {
    InvMarch0, InvMarch1, InvMarch2, InvMarch3,  // the fleet's four-note step
    InvShot, InvAlienHit, InvPlayerHit, InvUfo, InvUfoHit, InvWave,
    MazeStart, MazeChompA, MazeChompB, MazeSiren, MazePower, MazeEatGhost, MazeFruit, MazeDeath, MazeLevel,
    Count
};

void Play(Id id);      // one shot
void Loop(Id id);      // keeps repeating until StopLoop; calling it again while looping does nothing
void StopLoop(Id id);
void StopAll();
bool Muted();
void SetMuted(bool muted);
void Shutdown();

}  // namespace sfx
