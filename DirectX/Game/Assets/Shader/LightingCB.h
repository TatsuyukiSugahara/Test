#define MAX_POINT_LIGHTS       8
#define MAX_DIRECTIONAL_LIGHTS 4

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
    DirectionalLight directionals[MAX_DIRECTIONAL_LIGHTS];
    uint             directionalLightCount;
    float            globalSpecularScale;
    uint             _lpad0;
    uint             _lpad1;
    PointLight       pointLights[MAX_POINT_LIGHTS];
    uint             pointLightCount;
    float3           cameraPosition;
};
