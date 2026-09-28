#ifndef TIMELINE_H
#define TIMELINE_H

#include <cstdint>

class Timeline {
public:
    // real time, root timeline
    Timeline(double tic = 1.0);

    // timeline anchored to another timeline
    Timeline(Timeline* anchor, double tic = 1.0);

    // current time represented by this timeline
    int64_t getTime() const;

    // elapsed game time in second since the previous update
    double getDeltaTime();

    // Pause controls
    void pause();
    void unpause();
    bool isPaused() const;

    // Timeline scale
    void setScale(double scale);
    double getScale() const;

    void setTic(double tic);
    double getTic() const;

private:
    // optinal parent timeline
    Timeline* anchor;

    double tic;
    double scale;
    bool paused;
    int64_t startTime;
    int64_t lastTime;
    int64_t pausedTime;

    // Return time from anchor real time
    int64_t getSourceTime() const;
};

#endif