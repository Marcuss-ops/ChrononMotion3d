#ifndef THREEPP_MOTION_PROCEDURAL_IDLE_POSE_HPP
#define THREEPP_MOTION_PROCEDURAL_IDLE_POSE_HPP

#include "threepp/math/Euler.hpp"
#include "threepp/math/Vector3.hpp"

namespace threepp {

    class Object3D;

    namespace motion {

        /// Small deterministic idle motion for static or non-rigged subjects.
        /// It animates the whole node; it cannot articulate individual limbs.
        class ProceduralIdlePose {
        public:
            struct Options {
                float cycleSeconds{7.5f};
                float yawRadians{0.022f};
                float leanRadians{0.012f};
                float bobAmplitude{0.006f};
                float phaseRadians{0.f};
            };

            explicit ProceduralIdlePose(Object3D& subject);
            ProceduralIdlePose(Object3D& subject, Options options);

            /// Samples directly from absolute timeline seconds, so any frame
            /// can be rendered out of order and still match a sequential render.
            void apply(float timelineSeconds) const;

        private:
            Object3D* subject_;
            Vector3 basePosition_;
            Euler baseRotation_;
            Options options_;
        };

    }// namespace motion
}// namespace threepp

#endif
