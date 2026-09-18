// ChrononMotion — the preset vocabulary: the one frame conversion and the two
// direction/axis tables. Kept in their own translation unit because guessing at
// them is how two families would disagree about "left" or about what a frame is.

#include "chrononmotion/motion/Presets.hpp"
#include "chrononmotion/motion/MotionScene.hpp"

namespace chrononmotion::motion {

    float frameToTime(int frame, float fps) {

        return MotionTime::atFrame(frame, fps).pts();
    }

    namespace presets {

        Vector3 directionOffset(Direction direction) {

            switch (direction) {
                case Direction::Left:
                    return Vector3(-1.f, 0.f, 0.f);
                case Direction::Right:
                    return Vector3(1.f, 0.f, 0.f);
                case Direction::Up:
                    return Vector3(0.f, 1.f, 0.f);
                case Direction::Down:
                    return Vector3(0.f, -1.f, 0.f);
            }
            return Vector3(0.f, 0.f, 0.f);
        }

        Vector3 axisVector(Axis axis) {

            switch (axis) {
                case Axis::X:
                    return Vector3(1.f, 0.f, 0.f);
                case Axis::Y:
                    return Vector3(0.f, 1.f, 0.f);
                case Axis::Z:
                    return Vector3(0.f, 0.f, 1.f);
            }
            return Vector3(0.f, 0.f, 0.f);
        }

    }// namespace presets

}// namespace chrononmotion::motion
