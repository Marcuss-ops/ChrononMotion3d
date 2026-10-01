#include "chrononmotion/motion/RigBaking.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <set>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <limits>

namespace chrononmotion::motion {
    namespace {
        constexpr std::size_t sampleBudget = 1000000;
        template<class T> std::size_t findId(const std::vector<T>& values, std::uint32_t id) {
            const auto it = std::find_if(values.begin(), values.end(), [id](const T& value) { return value.id == id; });
            if (it == values.end()) throw std::invalid_argument("RigProgram: missing reference");
            return static_cast<std::size_t>(it - values.begin());
        }
        template<class T> void uniqueIds(std::vector<T>& values) {
            std::sort(values.begin(), values.end(), [](const T& a, const T& b) { return a.id < b.id; });
            for (std::size_t i = 0; i < values.size(); ++i)
                if (!values[i].id || (i && values[i].id == values[i - 1].id))
                    throw std::invalid_argument("RigProgram: duplicate or zero operator ID");
        }
        template<class T> void validateTrack(const Track<T>& track) {
            if (!track.modifiers().empty() || track.keys().size() > sampleBudget)
                throw std::invalid_argument("RigProgram: tracks must be bounded and self-contained");
            float previous = -1.f;
            for (const auto& key : track.keys()) {
                if (!std::isfinite(key.time) || key.time < 0.f || key.time <= previous)
                    throw std::invalid_argument("RigProgram: invalid track time");
                previous = key.time;
                if constexpr (std::is_same_v<T, float>) {
                    if (!std::isfinite(key.value)) throw std::invalid_argument("RigProgram: nonfinite track value");
                } else if constexpr (std::is_same_v<T, Vector3>) {
                    if (!std::isfinite(key.value.x) || !std::isfinite(key.value.y) || !std::isfinite(key.value.z))
                        throw std::invalid_argument("RigProgram: nonfinite track vector");
                } else {
                    if (!std::isfinite(key.value.lengthSq()) || std::abs(key.value.lengthSq() - 1.f) > 1e-4f)
                        throw std::invalid_argument("RigProgram: invalid track quaternion");
                }
            }
        }
        RigPose locals(const RigEvaluation& evaluation) {
            RigPose pose;
            for (std::size_t i = 0; i < evaluation.ids.size(); ++i) pose.push_back({evaluation.ids[i], evaluation.local[i]});
            return pose;
        }
        struct Hash {
            std::uint64_t value{14695981039346656037ULL};
            void integer(std::uint64_t x) {
                for (unsigned byte = 0; byte < 8; ++byte) { value ^= (x >> (byte * 8U)) & 255U; value *= 1099511628211ULL; }
            }
            void number(float x) {
                if (!std::isfinite(x)) throw std::invalid_argument("RigProgram: nonfinite signature input");
                integer(std::bit_cast<std::uint32_t>(x));
            }
            void vector(const Vector3& v) { number(v.x); number(v.y); number(v.z); }
            void quaternion(const Quaternion& q) { number(q.x); number(q.y); number(q.z); number(q.w); }
            void transform(const Transform& t) {
                vector(t.position); vector(t.anchor); vector(t.scale); vector(t.rotation); quaternion(t.orientation); number(t.opacity);
            }
            template<class T> void track(const Track<T>& t) {
                integer(t.keys().size());
                for (const auto& key : t.keys()) {
                    number(key.time);
                    if constexpr (std::is_same_v<T, float>) number(key.value);
                    else if constexpr (std::is_same_v<T, Vector3>) vector(key.value);
                    else quaternion(key.value);
                }
            }
        };
    }
    std::uint64_t rigFrameSignature(const RigProgramFrame& frame) {
        Hash hash;
        if (frame.pose.ids.size() != frame.pose.local.size() || frame.pose.ids.size() != frame.pose.world.size())
            throw std::invalid_argument("RigProgram: invalid signature pose");
        hash.integer(frame.pose.ids.size());
        for (std::size_t i = 0; i < frame.pose.ids.size(); ++i) {
            hash.integer(frame.pose.ids[i]); hash.transform(frame.pose.local[i]);
            for (const float value : frame.pose.world[i].elements) hash.number(value);
        }
        hash.integer(frame.geometry.size());
        for (const auto& geometry : frame.geometry) {
            hash.integer(geometry.id); hash.integer(geometry.positions.size());
            for (const auto& position : geometry.positions) hash.vector(position);
        }
        return hash.value;
    }

    RigProgramEvaluator::RigProgramEvaluator(const RigProgram& program)
        : program_(program), rig_(program.graph.compile()), raw_(rig_.withoutConstraints()), drivers_(raw_, program.drivers) {
        if (program_.ik.size() > 256 || program_.geometry.size() > 10000 || program_.weights.size() > 10000 ||
            program_.poses.size() > 1024 || program_.animation.size() > raw_.nodes().size())
            throw std::invalid_argument("RigProgram: operator budget exceeded");
        uniqueIds(program_.ik); uniqueIds(program_.geometry); uniqueIds(program_.animation);
        for (const auto& node : program_.animation) {
            animationNodes_.push_back(findId(raw_.nodes(), node.id));
            validateTrack(node.tracks.position); validateTrack(node.tracks.anchor); validateTrack(node.tracks.rotation);
            validateTrack(node.tracks.scale); validateTrack(node.tracks.orientation); validateTrack(node.tracks.opacity);
            // Check complete poses at every keyed time, not just at frame zero.
            Layer layer; layer.transform = raw_.nodes()[findId(raw_.nodes(), node.id)].rest; layer.tracks = node.tracks;
            const auto check = [&](const auto& track) {
                for (const auto& key : track.keys()) (void) raw_.evaluateFK(RigPose{{node.id, layer.sample(key.time)}});
            };
            check(node.tracks.position); check(node.tracks.anchor); check(node.tracks.rotation);
            check(node.tracks.scale); check(node.tracks.orientation); check(node.tracks.opacity);
        }
        std::set<RigNodeId> writtenIK;
        for (const auto& ik : program_.ik) {
            validateTrack(ik.target); validateTrack(ik.pole); validateTrack(ik.amount);
            for (const auto& key : ik.amount.keys()) if (key.value < 0.f || key.value > 1.f)
                throw std::invalid_argument("RigProgram: invalid IK weight track");
            ikTargets_.push_back(ik.targetNode ? findId(raw_.nodes(), ik.targetNode) : raw_.nodes().size());
            ikPoles_.push_back(ik.poleNode ? findId(raw_.nodes(), ik.poleNode) : raw_.nodes().size());
            if (ik.targetNode && !ik.target.empty()) throw std::invalid_argument("RigProgram: ambiguous IK target");
            if (ik.poleNode && !ik.pole.empty()) throw std::invalid_argument("RigProgram: ambiguous IK pole");
            for (const auto id : ik.chain.joints) {
                if (id == ik.targetNode || id == ik.poleNode || !writtenIK.insert(id).second)
                    throw std::invalid_argument("RigProgram: overlapping or feedback IK chains");
            }
            (void) raw_.solveIK(ik.chain);
        }
        // IK controls cannot depend on a solved chain through their hierarchy.
        for (const auto& ik : program_.ik) for (auto id : {ik.targetNode, ik.poleNode}) {
            while (id) {
                if (writtenIK.contains(id)) throw std::invalid_argument("RigProgram: IK control feedback");
                id = raw_.nodes()[findId(raw_.nodes(), id)].parent;
            }
        }
        std::size_t vertices = 0;
        for (const auto& geometry : program_.geometry) {
            if (geometry.source.size() > sampleBudget - vertices) throw std::invalid_argument("RigProgram: geometry budget exceeded");
            vertices += geometry.source.size();
            if (geometry.morphTracks.size() > geometry.morphs.size()) throw std::invalid_argument("RigProgram: morph track topology mismatch");
            for (const auto& track : geometry.morphTracks) {
                validateTrack(track);
                for (const auto& key : track.keys()) if (key.value < 0.f || key.value > 1.f)
                    throw std::invalid_argument("RigProgram: invalid morph weight track");
            }
            if (geometry.rigidNode) (void) findId(raw_.nodes(), geometry.rigidNode);
            if (geometry.rigidNode && !geometry.skin.empty()) throw std::invalid_argument("RigProgram: rigid and skin binding conflict");
            if (geometry.indices.size() > sampleBudget * 3 || geometry.indices.size() % 3)
                throw std::invalid_argument("RigProgram: invalid triangle index budget");
            for (const auto index : geometry.indices) if (index >= geometry.source.size())
                throw std::invalid_argument("RigProgram: triangle index out of range");
            if (geometry.skin.empty() && !geometry.inverseBinds.empty())
                throw std::invalid_argument("RigProgram: inverse binds require skin");
            const auto morphed = mixRigMorphs(geometry.source, geometry.morphs);
            (void) geometry.deformers.evaluate(morphed);
            if (!geometry.skin.empty()) (void) skinRigGeometry(raw_, raw_.bindPose(), geometry.source, geometry.skin, geometry.inverseBinds);
        }
        std::set<std::tuple<RigWeightKind, std::uint32_t, unsigned>> weights;
        for (const auto& binding : program_.weights) {
            const auto source = findId(raw_.nodes(), binding.source);
            if (raw_.nodes()[source].kind != RigNodeKind::Controller ||
                !weights.emplace(binding.kind, binding.target, binding.component).second)
                throw std::invalid_argument("RigProgram: invalid or ambiguous weight binding");
            std::size_t target = 0;
            switch (binding.kind) {
                case RigWeightKind::IK:
                    target = findId(program_.ik, binding.target);
                    if (!program_.ik[target].amount.empty() || binding.component != 0)
                        throw std::invalid_argument("RigProgram: ambiguous IK weight authority");
                    break;
                case RigWeightKind::Constraint:
                    target = findId(rig_.nodes(), binding.target);
                    if (binding.component >= rig_.nodes()[target].constraints.size())
                        throw std::invalid_argument("RigProgram: missing constraint weight target");
                    break;
                case RigWeightKind::Deformer:
                    target = findId(program_.geometry, binding.target);
                    if (binding.component >= program_.geometry[target].deformers.operators.size())
                        throw std::invalid_argument("RigProgram: missing deformer weight target");
                    break;
                case RigWeightKind::Morph:
                    target = findId(program_.geometry, binding.target);
                    if (binding.component < program_.geometry[target].morphTracks.size() &&
                        !program_.geometry[target].morphTracks[binding.component].empty())
                        throw std::invalid_argument("RigProgram: ambiguous morph weight authority");
                    if (binding.component >= program_.geometry[target].morphs.size())
                        throw std::invalid_argument("RigProgram: missing morph weight target");
                    break;
                case RigWeightKind::Pose:
                    target = binding.target;
                    if (binding.component != 0 || binding.target >= program_.poses.size()) throw std::invalid_argument("RigProgram: missing pose weight target");
                    break;
                default: throw std::invalid_argument("RigProgram: unknown weight kind");
            }
            weights_.push_back({source, target, binding.kind, binding.component});
        }
        (void) mixRigPoses(raw_, {}, program_.poses);
        if (!std::isfinite(program_.secondaryFps) || program_.secondaryFps <= 0.f || program_.secondaryFirstFrame < 0 ||
            program_.secondary.size() > sampleBudget / std::max<std::size_t>(1, raw_.nodes().size()))
            throw std::invalid_argument("RigProgram: invalid secondary sample budget");
        for (const auto& pose : program_.secondary) {
            (void) raw_.evaluateFK(pose);
            secondaryNodes_.emplace_back();
            for (const auto& value : pose) secondaryNodes_.back().push_back(findId(raw_.nodes(), value.id));
        }
        // Validates space references/events/cycles at authoring time.
        if (!program_.spaces.empty()) (void) evaluateRigSpaces(raw_, program_.animation, program_.spaces, 0.f);
        (void) evaluate(program_.secondary.empty() ? 0 : program_.secondaryFirstFrame,
                        program_.secondary.empty() ? 60.f : program_.secondaryFps);
    }

    RigProgramFrame RigProgramEvaluator::evaluate(int frame, float fps, std::span<const RigControllerValue> controls) const {
        if (frame < 0 || !std::isfinite(fps) || fps <= 0.f)
            throw std::invalid_argument("RigProgram: invalid frame time");
        const float seconds = static_cast<float>(frame) / fps;
        if (!std::isfinite(seconds)) throw std::invalid_argument("RigProgram: time overflow");
        RigPose pose;
        if (!program_.spaces.empty()) pose = evaluateRigSpaces(raw_, program_.animation, program_.spaces, seconds);
        else {
            for (std::size_t i = 0; i < program_.animation.size(); ++i) {
                const auto& animated = program_.animation[i];
                Layer layer; layer.transform = raw_.nodes()[animationNodes_[i]].rest; layer.tracks = animated.tracks;
                pose.push_back({animated.id, layer.sample(seconds)});
            }
        }
        pose = drivers_.evaluate(seconds, controls, pose);
        auto layers = program_.poses;
        auto ik = program_.ik;
        auto geometry = program_.geometry;
        for (auto& binding : geometry) for (std::size_t i = 0; i < binding.morphTracks.size(); ++i)
            if (!binding.morphTracks[i].empty()) binding.morphs[i].weight = binding.morphTracks[i].sample(seconds);
        std::vector<RigConstraintWeight> constraintWeights;
        for (const auto& binding : weights_) {
            const float weight = std::clamp(pose[binding.source].local.position.x, 0.f, 1.f);
            switch (binding.kind) {
                case RigWeightKind::IK: ik[binding.target].chain.amount = weight; break;
                case RigWeightKind::Pose: layers[binding.target].weight = weight; break;
                case RigWeightKind::Deformer: geometry[binding.target].deformers.operators[binding.component].amount = weight; break;
                case RigWeightKind::Morph: geometry[binding.target].morphs[binding.component].weight = weight; break;
                case RigWeightKind::Constraint:
                    constraintWeights.push_back({raw_.nodes()[binding.target].id, binding.component, weight});
                    break;
            }
        }
        pose = mixRigPoses(raw_, pose, layers);
        for (std::size_t i = 0; i < ik.size(); ++i) {
            auto& solver = ik[i];
            const auto input = raw_.evaluateFK(pose);
            if (solver.targetNode) solver.chain.target.setFromMatrixPosition(input.world[ikTargets_[i]]);
            else if (!solver.target.empty()) solver.chain.target = solver.target.sample(seconds);
            if (solver.poleNode) solver.chain.pole.setFromMatrixPosition(input.world[ikPoles_[i]]);
            else if (!solver.pole.empty()) solver.chain.pole = solver.pole.sample(seconds);
            if (!solver.amount.empty()) solver.chain.amount = solver.amount.sample(seconds);
            pose = raw_.solveIK(solver.chain, pose);
        }
        RigProgramFrame output;
        output.pose = rig_.evaluateAt(seconds, pose, constraintWeights);
        if (!program_.secondary.empty()) {
            if (fps != program_.secondaryFps || frame < program_.secondaryFirstFrame ||
                static_cast<std::size_t>(frame - program_.secondaryFirstFrame) >= program_.secondary.size())
                throw std::invalid_argument("RigProgram: secondary sample outside baked range");
            pose = locals(output.pose);
            const auto sample = static_cast<std::size_t>(frame - program_.secondaryFirstFrame);
            for (std::size_t i = 0; i < program_.secondary[sample].size(); ++i)
                pose[secondaryNodes_[sample][i]].local = program_.secondary[sample][i].local;
            output.pose = raw_.evaluateFK(pose);
        }
        for (const auto& binding : geometry) {
            auto positions = mixRigMorphs(binding.source, binding.morphs);
            positions = binding.deformers.evaluate(positions);
            if (!binding.skin.empty()) positions = skinRigGeometry(raw_, output.pose, positions, binding.skin, binding.inverseBinds);
            else if (binding.rigidNode) for (auto& point : positions) point = output.pose.localToWorld(binding.rigidNode, point);
            output.geometry.push_back({binding.id, std::move(positions)});
        }
        (void) rigFrameSignature(output); // Finite final pose/geometry gate.
        return output;
    }

    BakedRigProgram RigProgramEvaluator::bake(int firstFrame, int lastFrame, float fps) const {
        if (firstFrame < 0 || lastFrame < firstFrame || !std::isfinite(fps) || fps <= 0.f)
            throw std::invalid_argument("RigProgram: invalid bake range");
        const auto count = static_cast<std::uint64_t>(lastFrame) - firstFrame + 1;
        std::size_t samples = raw_.nodes().size();
        for (const auto& geometry : program_.geometry) samples += geometry.source.size();
        if (count > sampleBudget / std::max<std::size_t>(1, samples) || lastFrame == std::numeric_limits<int>::max()) throw std::invalid_argument("RigProgram: bake sample budget exceeded");
        BakedRigProgram result;
        std::vector<RigPose> poses;
        Hash signature;
        signature.integer(firstFrame); signature.integer(lastFrame); signature.number(fps);
        for (std::uint64_t offset = 0; offset < count; ++offset) {
            result.frames.push_back(evaluate(firstFrame + static_cast<int>(offset), fps));
            poses.push_back(locals(result.frames.back().pose));
            signature.integer(rigFrameSignature(result.frames.back()));
        }
        result.transforms = raw_.bake(poses, firstFrame, fps);
        for (const auto& node : result.transforms.nodes) {
            signature.integer(node.id); signature.integer(node.layerId);
            signature.track(node.tracks.position); signature.track(node.tracks.anchor); signature.track(node.tracks.rotation);
            signature.track(node.tracks.orientation); signature.track(node.tracks.scale); signature.track(node.tracks.opacity);
        }
        result.signature = signature.value;
        return result;
    }
}
