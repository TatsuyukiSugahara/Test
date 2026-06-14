#define MAX_POINT_LIGHTS 8

struct AmbientLight
{
    float3 color;
    float  intensity;
};

struct DirectionalLight
{
    float3 direction;  // world 空間、正規化、光源→シーン方向
    float  intensity;
    float3 color;
    float  pad;
};

struct PointLight
{
    float3 position;
    float  range;
    float3 color;
    float  intensity;
};

cbuffer LightingCB : register(b1)
{
    AmbientLight     ambient;
    DirectionalLight directional;
    PointLight       pointLights[MAX_POINT_LIGHTS];
    uint             pointLightCount;
    float3           cameraPosition;
};
