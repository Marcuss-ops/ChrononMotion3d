#include "chrononmotion/geometries/LatheGeometry.hpp"

#include <algorithm>
#include <cmath>

using namespace chrononmotion;

LatheGeometry::LatheGeometry(const std::vector<Vector2>& points, unsigned int segments, float phiStart, float phiLength)
    : parameters(points, segments, phiStart, phiLength) {

    phiLength = std::clamp(phiLength, 0.f, math::TWO_PI);

    std::vector<unsigned int> indices;
    std::vector<float> vertices;
    std::vector<float> uvs;

    const auto inverseSegments = 1.f / static_cast<float>(segments);
    Vector3 vertex;
    Vector2 uv;

    for (unsigned i = 0; i <= segments; i++) {

        const auto phi = phiStart + static_cast<float>(i) * inverseSegments * phiLength;

        const auto sin = std::sin(phi);
        const auto cos = std::cos(phi);

        for (unsigned j = 0; j < points.size(); j++) {

            vertex.x = points[j].x * sin;
            vertex.y = points[j].y;
            vertex.z = points[j].x * cos;

            vertices.insert(vertices.end(), {vertex.x, vertex.y, vertex.z});

            uv.x = static_cast<float>(i) / static_cast<float>(segments);
            uv.y = static_cast<float>(j) / static_cast<float>(points.size() > 1 ? points.size() - 1 : 1);

            uvs.insert(uvs.end(), {uv.x, uv.y});
        }
    }

    for (unsigned i = 0; i < segments; i++) {

        for (unsigned j = 0; j < (points.size() > 1 ? points.size() - 1 : 0); j++) {

            const auto base = j + i * points.size();

            const unsigned int a = base;
            const unsigned int b = base + points.size();
            const unsigned int c = base + points.size() + 1;
            const unsigned int d = base + 1;

            indices.insert(indices.end(), {a, b, d});
            indices.insert(indices.end(), {b, c, d});
        }
    }

    this->setIndex(indices);
    this->setAttribute("position", FloatBufferAttribute::create(vertices, 3));
    this->setAttribute("uv", FloatBufferAttribute::create(uvs, 2));

    this->computeVertexNormals();

    if (phiLength == math::TWO_PI) {
        auto* normAttr = getAttribute<float>("normal");
        if (normAttr) {
            auto& normals = normAttr->array();
            Vector3 n1;
            Vector3 n2;
            Vector3 n;

            const auto base = segments * points.size() * 3;

            for (unsigned i = 0, j = 0; i < points.size(); i++, j += 3) {
                if (j + 2 < normals.size() && base + j + 2 < normals.size()) {
                    n1.x = normals[j];
                    n1.y = normals[j + 1];
                    n1.z = normals[j + 2];

                    n2.x = normals[base + j];
                    n2.y = normals[base + j + 1];
                    n2.z = normals[base + j + 2];

                    n.addVectors(n1, n2).normalize();

                    normals[j] = normals[base + j] = n.x;
                    normals[j + 1] = normals[base + j + 1] = n.y;
                    normals[j + 2] = normals[base + j + 2] = n.z;
                }
            }
        }
    }
}

std::string LatheGeometry::type() const {
    return "LatheGeometry";
}

std::shared_ptr<LatheGeometry> LatheGeometry::create(const Params& params) {
    return std::shared_ptr<LatheGeometry>(new LatheGeometry(params.points, params.segments, params.phiStart, params.phiLength));
}

std::shared_ptr<LatheGeometry> LatheGeometry::create(const std::vector<Vector2>& points, unsigned int segments, float phiStart, float phiLength) {
    return std::shared_ptr<LatheGeometry>(new LatheGeometry(points, segments, phiStart, phiLength));
}
