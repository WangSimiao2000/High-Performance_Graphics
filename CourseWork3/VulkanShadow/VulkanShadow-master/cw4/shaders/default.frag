#version 450

#define MAX_LIGHT_NUM 10

layout( location = 0 ) in vec2 v2f_tc;
layout( location = 1 ) in vec4 pos_world;
layout( location = 2 ) in vec3 normal_world;

layout( location = 0) out vec4 oColor;

layout( set = 0, binding = 0 ) uniform UScene
{
    mat4 M;
    mat4 V;
    mat4 P;
    vec3 camPos;
    int light_count;
} uScene;

struct LightInfo {
    vec4 color;
    vec3 pos;
    float max_depth;
};

layout( set = 1, binding = 0 ) uniform Light
{
    LightInfo lights[MAX_LIGHT_NUM];
} uLight;

layout( set = 2, binding = 0 ) uniform samplerCubeArrayShadow shadowMap;

layout( set = 3, binding = 0 ) uniform sampler2D baseColor;
layout( set = 3, binding = 1 ) uniform sampler2D metalness;
layout( set = 3, binding = 2 ) uniform sampler2D roughness;
layout( set = 3, binding = 3 ) uniform sampler2D alphaMask;
layout( set = 3, binding = 4 ) uniform sampler2D normalMap;

const float PI = 3.14159265359;

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}  

float NormalDF(float NdotH, float roughness)
{
    float ap = 2.0 / (roughness * roughness * roughness * roughness + 0.001) - 2.0;
    return (ap + 2.0) / (2.0 * PI) * pow(NdotH, ap);
}

float GeometryDF(float NDotH, float NDotL, float VDotH, float NDotV, float roughness)
{
    float left = 2.0 * NDotH * NDotV / VDotH;
    float right = 2.0 * NDotH * NDotL / VDotH;
    return min(1.0, min(left, right));
}

vec3 getNormalFromMap(vec3 normal)
{
    if (textureSize(normalMap, 0).x == 1.0) {
        return normal;
    }
    vec3 tangentNormal = texture(normalMap, v2f_tc).xyz * 2.0 - 1.0;

    vec3 Q1  = dFdx(pos_world.xyz);
    vec3 Q2  = dFdy(pos_world.xyz);
    vec2 st1 = dFdx(v2f_tc);
    vec2 st2 = dFdy(v2f_tc);

    vec3 N   = normalize(normal);
    vec3 T  = normalize(Q1*st2.t - Q2*st1.t);
    vec3 B  = -normalize(cross(N, T));
    mat3 TBN = mat3(T, B, N);

    return normalize(TBN * tangentNormal);
}

float getShadow(vec3 light_dir, int which_light) {
    float max_depth = uLight.lights[which_light].max_depth;
    float current_depth = length(light_dir) / max_depth;
    float has_shadow = texture(shadowMap, vec4(normalize(light_dir), which_light), current_depth - 0.01);
    return has_shadow;
}

void main()
{   
    if (texture(alphaMask, v2f_tc).a < 0.5)
    {
        discard;
    }
    vec3 albedo = texture(baseColor, v2f_tc).rgb;
    float metallic = texture(metalness, v2f_tc).r;
    float roughness = texture(roughness, v2f_tc).r;

    // NORMAL MAP
    vec3 N = getNormalFromMap(normal_world);

    // for each light:
    vec3 lighting = vec3(0);
    for (int light_id = 0; light_id < uScene.light_count && light_id < MAX_LIGHT_NUM; light_id ++) {
        // calculate view and light direction
        vec3 L = normalize(uLight.lights[light_id].pos - vec3(pos_world));
        vec3 V = normalize(uScene.camPos - vec3(pos_world));

        vec3 H = normalize(V + L);

        float NDotH = max(dot(N, H), 0.0);
        float NDotL = max(dot(N, L), 0.0);
        float VDotH = max(dot(V, H), 0.0);
        float NDotV = max(dot(N, V), 0.0);

        // brdf
        float NDF = NormalDF(NDotH, roughness);        
        float G   = GeometryDF(NDotH, NDotL, VDotH, NDotV, roughness);  
        
        vec3 F0 = vec3(0.04); 
        F0 = mix(F0, albedo, metallic);    
        vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);       

        vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

        vec3 specular     = (NDF * G * F) / (4.0 * NDotV * NDotL + 0.001);

        // add to outgoing radiance Lo
        float NdotL = max(dot(N, L), 0.0);                
        vec3 radiance = uLight.lights[light_id].color.rgb * (1.0 - getShadow(vec3(pos_world) - uLight.lights[light_id].pos, light_id));
        lighting += (kD * albedo / PI + specular) * NdotL * radiance;
    }
    
    vec3 color = lighting + albedo * 0.02;

    oColor = vec4(color, 1.0);
}