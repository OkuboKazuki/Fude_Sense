#include "stdafx.h"
#include "ScreenLayout.h"

void ScreenLayout::Layout(AppState& state, int clientWidth, int clientHeight) {
    state.Layout(clientWidth, clientHeight);
}

void ScreenLayout::UpdateReplaySeekThumb(AppState& state) {
    state.UpdateReplaySeekThumb();
}
