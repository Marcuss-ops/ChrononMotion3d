#include "chrononmotion/geometries/TubeGeometry.hpp"

#include <cmath>
#include <functional>
#include <vector>

using namespace chrononmotion;

TubeGeometry::TubeGeometry(std::variant<Curve3*, std::shared_ptr<Curve3>> path, const Params& params)
    : radius(params.radius), path_(std::move(path)) {

    this->frames_ = FrenetFrames::compute(*getPath(), params.tubularSegments, params.closed);

    Vector3 vertex;
    Vector3 normal;
    Vector2 uv;
    Vector3 P;

    std::vector<float> vertices;
    std::vector<float> normals;
    std::vector<float> uvs;
    std::vector<unsigned int> indices;

    auto generateSegment = [&](unsigned int i) {
        this->getPath()->getPointAt(static_cast<float>(i) / static_cast<float>(params.tubularSegments), P);

        const Vector3& N = frames_.normals[i];
        const Vector3& B = frames_.binormals[i];

        for (unsigned j = 0; j <= params.radialSegments; j++) {

            const float v = static_cast<float>(j) / static_cast<float>(params.radialSegments) * math::TWO_PI;

            const float sin = std::sin(v);
            const float cos = -std::cos(v);

            normal.x = (cos * N.x + sin * B.x);
            normal.y = (cos * N.y + sin * B.y);
            normal.z = (cos * N.z + sin * B.z);
            normal.normalize();

            normals.insert(normals.end(), {normal.x, normal.y, normal.z});

            vertex.x = P.x + radius * normal.x;
            vertex.y = P.y + radius * normal.y;
            vertex.z = P.z + radius * normal.z;

            vertices.insert(vertices.end(), {vertex.x, vertex.y, vertex.z});

            uv.x = static_cast<float>(i) / static_cast<float>(params.tubularSegments);
            uv.y = static_cast<float>(j) / static_cast<float>(params.radialSegments);

            uvs.insert(uvs.end(), {uv.x, uv.y});
        }
    };

    for (unsigned i = 0; i < params.tubularSegments; i++) {
        generateSegment(i);
    }

    generateSegment((!params.closed) ? params.tubularSegments : 0);

    for (unsigned j = 1; j <= params.tubularSegments; j++) {

        for (unsigned i = 1; i <= params.radialSegments; i++) {

            const auto a = (params.radialSegments + 1) * (j - 1) + (i - 1);
            const auto b = (params.radialSegments + 1) * j + (i - 1);
            const auto c = (params.radialSegments + 1) * j + i;
            const auto d = (params.radialSegments + 1) * (j - 1) + i;

            indices.insert(indices.end(), {a, b, d});
            indices.insert(indices.end(), {b, c, d});
        }
    }

    if (!params.closed) {
        // Cap the start of the tube with a planar circular fan
        Vector3 startP, startT;
        this->getPath()->getPointAt(0.f, startP);
        this->getPath()->getTangentAt(0.f, startT);
        Vector3 startN = startT.normalize().negate();
        const auto startCenterIdx = static_cast<unsigned int>(vertices.size() / 3);
        vertices.insert(vertices.end(), {startP.x, startP.y, startP.z});
        normals.insert(normals.end(), {startN.x, startN.y, startN.z});
        uvs.insert(uvs.end(), {0.5f, 0.5f});
        for (unsigned i = 0; i < params.radialSegments; i++) {
            indices.insert(indices.end(), {startCenterIdx, i + 1, i});
        }

        // Cap the end of the tube with a planar circular fan
        Vector3 endP, endT;
        this->getPath()->getPointAt(1.f, endP);
        this->getPath()->getTangentAt(1.f, endT);
        Vector3 endN = endT.normalize();
        const auto endCenterIdx = static_cast<unsigned int>(vertices.size() / 3);
        vertices.insert(vertices.end(), {endP.x, endP.y, endP.z});
        normals.insert(normals.end(), {endN.x, endN.y, endN.z});
        uvs.insert(uvs.end(), {0.5f, 0.5f});
        const unsigned int lastRingBase = params.tubularSegments * (params.radialSegments + 1);
        for (unsigned i = 0; i < params.radialSegments; i++) {
            indices.insert(indices.end(), {endCenterIdx, lastRingBase + i, lastRingBase + i + 1});
        }
    }

    this->setIndex(indices);
    this->setAttribute("position", FloatBufferAttribute::create(vertices, 3));
    this->setAttribute("normal", FloatBufferAttribute::create(normals, 3));
    this->setAttribute("uv", FloatBufferAttribute::create(uvs, 2));
}

std::string TubeGeometry::type() const {
    return "TubeGeometry";
}

std::shared_ptr<TubeGeometry> TubeGeometry::create(std::variant<Curve3*, std::shared_ptr<Curve3>> path, const Params& params) {
    return std::shared_ptr<TubeGeometry>(new TubeGeometry(std::move(path), params));
}

std::shared_ptr<TubeGeometry> TubeGeometry::create(std::variant<Curve3*, std::shared_ptr<Curve3>> path, unsigned int tubularSegments, float radius, unsigned int radialSegments, bool closed) {
    return create(std::move(path), Params(tubularSegments, radius, radialSegments, closed));
}

TubeGeometry::Params::Params(unsigned int tubularSegments, float radius, unsigned int radialSegments, bool closed)
    : tubularSegments(tubularSegments), radius(radius), radialSegments(radialSegments), closed(closed) {}
