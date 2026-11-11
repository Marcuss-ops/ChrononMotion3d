#include "chrononmotion/geometries/CylinderGeometry.hpp"

#include <cmath>
#include <vector>

using namespace chrononmotion;

CylinderGeometry::CylinderGeometry(const Params& params)
    : parameters(params),
      radiusTop(params.radiusTop),
      radiusBottom(params.radiusBottom),
      height(params.height) {

    std::vector<unsigned int> indices;
    std::vector<float> vertices;
    std::vector<float> normals;
    std::vector<float> uvs;

    unsigned int index = 0;
    const auto halfHeight = height / 2.f;
    std::vector<std::vector<unsigned int>> indexArray;
    int groupStart = 0;

    auto generateTorso = [&] {
        Vector3 normal;
        Vector3 vertex;

        int groupCount = 0;

        const auto slope = (radiusBottom - radiusTop) / height;

        for (unsigned y = 0; y <= params.heightSegments; y++) {

            std::vector<unsigned int> indexRow;

            const auto v = static_cast<float>(y) / static_cast<float>(params.heightSegments);
            const auto radius = v * (radiusBottom - radiusTop) + radiusTop;

            for (unsigned x = 0; x <= params.radialSegments; x++) {

                const auto u = static_cast<float>(x) / static_cast<float>(params.radialSegments);
                const auto theta = u * params.thetaLength + params.thetaStart;

                const auto sinTheta = std::sin(theta);
                const auto cosTheta = std::cos(theta);

                vertex.x = radius * sinTheta;
                vertex.y = -v * height + halfHeight;
                vertex.z = radius * cosTheta;
                vertices.insert(vertices.end(), {vertex.x, vertex.y, vertex.z});

                normal.set(sinTheta, slope, cosTheta).normalize();
                normals.insert(normals.end(), {normal.x, normal.y, normal.z});

                uvs.insert(uvs.end(), {u, 1.f - v});

                indexRow.emplace_back(index++);
            }

            indexArray.emplace_back(indexRow);
        }

        for (unsigned x = 0; x < params.radialSegments; x++) {

            for (unsigned y = 0; y < params.heightSegments; y++) {

                const auto a = indexArray[y][x];
                const auto b = indexArray[y + 1][x];
                const auto c = indexArray[y + 1][x + 1];
                const auto d = indexArray[y][x + 1];

                indices.insert(indices.end(), {a, b, d});
                indices.insert(indices.end(), {b, c, d});

                groupCount += 6;
            }
        }

        addGroup(groupStart, groupCount, 0);
        groupStart += groupCount;
    };

    generateTorso();

    auto generateCap = [&](bool top) {
        auto centerIndexStart = index;

        Vector2 uv;
        Vector3 vertex;

        int groupCount = 0;

        float radius = (top) ? radiusTop : radiusBottom;
        float sign = (top) ? 1.f : -1.f;

        for (unsigned x = 1; x <= params.radialSegments; x++) {
            vertices.insert(vertices.end(), {0.f, halfHeight * sign, 0.f});
            normals.insert(normals.end(), {0.f, sign, 0.f});
            uvs.insert(uvs.end(), {0.5f, 0.5f});
            index++;
        }

        const auto centerIndexEnd = index;

        for (unsigned x = 0; x <= params.radialSegments; x++) {

            float u = static_cast<float>(x) / static_cast<float>(params.radialSegments);
            float theta = u * params.thetaLength + params.thetaStart;

            float cosTheta = std::cos(theta);
            float sinTheta = std::sin(theta);

            vertex.x = radius * sinTheta;
            vertex.y = halfHeight * sign;
            vertex.z = radius * cosTheta;
            vertices.insert(vertices.end(), {vertex.x, vertex.y, vertex.z});

            normals.insert(normals.end(), {0.f, sign, 0.f});

            uv.x = (cosTheta * 0.5f) + 0.5f;
            uv.y = (sinTheta * 0.5f * sign) + 0.5f;
            uvs.insert(uvs.end(), {uv.x, uv.y});

            ++index;
        }

        for (unsigned x = 0; x < params.radialSegments; x++) {

            unsigned int c = centerIndexStart + x;
            unsigned int i = centerIndexEnd + x;

            if (top) {
                indices.insert(indices.end(), {i, i + 1, c});
            } else {
                indices.insert(indices.end(), {i + 1, i, c});
            }

            groupCount += 3;
        }

        addGroup(groupStart, groupCount, top ? 1 : 2);
        groupStart += groupCount;
    };

    if (!params.openEnded) {
        if (radiusTop > 0.f) generateCap(true);
        if (radiusBottom > 0.f) generateCap(false);
    }

    this->setIndex(indices);
    this->setAttribute("position", FloatBufferAttribute::create(vertices, 3));
    this->setAttribute("normal", FloatBufferAttribute::create(normals, 3));
    this->setAttribute("uv", FloatBufferAttribute::create(uvs, 2));
}

std::string CylinderGeometry::type() const {
    return "CylinderGeometry";
}

std::shared_ptr<CylinderGeometry> CylinderGeometry::create(const CylinderGeometry::Params& params) {
    return std::shared_ptr<CylinderGeometry>(new CylinderGeometry(params));
}

std::shared_ptr<CylinderGeometry> CylinderGeometry::create(float radiusTop, float radiusBottom, float height, unsigned int radialSegments, unsigned int heightSegments, bool openEnded, float thetaStart, float thetaLength) {
    return create(Params(radiusTop, radiusBottom, height, radialSegments, heightSegments, openEnded, thetaStart, thetaLength));
}

CylinderGeometry::Params::Params(float radiusTop, float radiusBottom, float height, unsigned int radialSegments, unsigned int heightSegments, bool openEnded, float thetaStart, float thetaLength)
    : radiusTop(radiusTop), radiusBottom(radiusBottom), height(height),
      radialSegments(radialSegments), heightSegments(heightSegments),
      openEnded(openEnded), thetaStart(thetaStart), thetaLength(thetaLength) {}
