/**
 * BoundingVolume.h
 * MeshからAABBを作成するクラス
 */
#pragma once


struct Bounds
{
	Vector3 minPoint = Vector3(FLT_MAX, FLT_MAX, FLT_MAX);
	Vector3 maxPoint = Vector3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	//
	void Compute(Model& model)
	{
		const auto& tkmFile = model.GetTkmFile();
		const auto& meshParts = tkmFile.GetMeshParts();
		//メッシュを一つづつ調べていく。
		for (const auto& mesh : meshParts) {
			for (const auto& vertex : mesh.vertexBuffer) {
				const auto& pos = vertex.pos;
				// 最小値から比較
				minPoint.x = min(minPoint.x, pos.x);
				minPoint.y = min(minPoint.y, pos.y);
				minPoint.z = min(minPoint.z, pos.z);
				// 最大値から比較
				maxPoint.x = max(maxPoint.x, pos.x);
				maxPoint.y = max(maxPoint.y, pos.y);
				maxPoint.z = max(maxPoint.z, pos.z);
			}
		}
	}
};





/**
 * 視錐台（フラスタム）
 */
struct Frustum
{
    /** 平面 */
    struct Plane {
        float a, b, c, d;

        void Normalize() {
            float len = std::sqrt(a * a + b * b + c * c);
            if (len > 1e-6f) { a /= len; b /= len; c /= len; d /= len; }
        }
        // 正 = 法線側（フラスタム内側）
        float SignedDistance(const Vector3& p) const {
            return a * p.x + b * p.y + c * p.z + d;
        }
    };

    enum { LEFT = 0, RIGHT, BOTTOM, TOP, NEAR_P, FAR_P };
    Plane planes[6];


    void BuildFromViewProjectionMatrix(const Matrix& vp)
    {
        // Gribb-Hartmann 法: VP 行列の行を足し引きして 6 平面を取り出す
        const auto& m = vp.m;

        planes[LEFT] = { m[0][3] + m[0][0], m[1][3] + m[1][0], m[2][3] + m[2][0], m[3][3] + m[3][0] };
        planes[RIGHT] = { m[0][3] - m[0][0], m[1][3] - m[1][0], m[2][3] - m[2][0], m[3][3] - m[3][0] };
        planes[BOTTOM] = { m[0][3] + m[0][1], m[1][3] + m[1][1], m[2][3] + m[2][1], m[3][3] + m[3][1] };
        planes[TOP] = { m[0][3] - m[0][1], m[1][3] - m[1][1], m[2][3] - m[2][1], m[3][3] - m[3][1] };
        planes[NEAR_P] = { m[0][3] + m[0][2], m[1][3] + m[1][2], m[2][3] + m[2][2], m[3][3] + m[3][2] };
        planes[FAR_P] = { m[0][3] - m[0][2], m[1][3] - m[1][2], m[2][3] - m[2][2], m[3][3] - m[3][2] };

        for (auto& p : planes) {
            p.Normalize();
        }
    }

    /** AABB vs フラスタム判定 */
    bool IsVisible(const Bounds& bounds) const
    {
        for (const auto& plane : planes) {
            // 法線方向に最も突き出した頂点（ポジティブ頂点）
            Vector3 pVtx = {
                plane.a >= 0.0f ? bounds.maxPoint.x : bounds.minPoint.x,
                plane.b >= 0.0f ? bounds.maxPoint.y : bounds.minPoint.y,
                plane.c >= 0.0f ? bounds.maxPoint.z : bounds.minPoint.z
            };
            // ポジティブ頂点すら平面の外側 → AABB 全体が外側
            if (plane.SignedDistance(pVtx) < 0.0f)
                return false;
        }
        return true;
    }
};