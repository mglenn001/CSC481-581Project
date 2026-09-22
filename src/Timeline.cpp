#include "Timeline.h"
#include <SDL3/SDL.h>

Timeline::Timeline(double tic)
{
    anchor = nullptr;

    this->tic = tic;
    scale = 1.0;

    paused = false;
    pausedTime = 0;

    startTime = getSourceTime();
    lastTime = startTime;
}

Timeline::Timeline(Timeline* anchor, double tic)
{
    this->anchor = anchor;

    this->tic = tic;
    scale = 1.0;

    paused = false;
    pausedTime = 0;

    startTime = getSourceTime();
    lastTime = startTime;
}

int64_t Timeline::getSourceTime() const
{
    // If this timeline is anchored to another timeline, use that timeline as the source of time
    if (anchor != nullptr) {
        return anchor->getTime();
    }

    // Otherwise use real time from SDL
    return static_cast<int64_t>(SDL_GetTicks());
}

int64_t Timeline::getTime() const
{
    if (paused) {
        return pausedTime;
    }

    int64_t elapsed = getSourceTime() - startTime;

    return static_cast<int64_t>(
        elapsed * scale / tic
    );
}

double Timeline::getDeltaTime()
{
    // When the timeline is paused, everything stop moving
    if (paused) {
        return 0.0;
    }

    int64_t currentTime = getSourceTime();

    int64_t elapsed = currentTime - lastTime;

    lastTime = currentTime;

    double seconds = static_cast<double>(elapsed) / 1000.0;

    return seconds * scale / tic;
}

void Timeline::pause()
{
    if (paused) {
        return;
    }

    pausedTime = getTime();
    paused = true;
}

void Timeline::unpause()
{
    if (!paused) {
        return;
    }

    int64_t currentSourceTime = getSourceTime();

    // Move the start time forward by the amount of real time spent paused.
    int64_t expectedSourceTime = startTime + static_cast<int64_t>(pausedTime * tic / scale);

    startTime += currentSourceTime - expectedSourceTime;

    lastTime = currentSourceTime;

    paused = false;
}

bool Timeline::isPaused() const
{
    return paused;
}

void Timeline::setScale(double scale)
{
    if (scale > 0.0) {
        this->scale = scale;
    }
}

double Timeline::getScale() const
{
    return scale;
}

void Timeline::setTic(double tic)
{
    if (tic > 0.0) {
        this->tic = tic;
    }
}

double Timeline::getTic() const
{
    return tic;
}