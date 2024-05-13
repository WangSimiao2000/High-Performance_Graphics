#version 450

layout( location = 0 ) in vec3 fragPos;
layout( location = 1 ) in vec2 fragTexCoord;
layout( location = 2 ) in vec3 fragNormal;
layout( location = 3 ) in vec3 viewDir;
layout( location = 4 ) in vec3 lightDir;
layout( location = 5 ) in vec3 lightCol;
layout( location = 6 ) in mat3 vTBN;

layout( set = 1, binding = 0 ) uniform sampler2D uTexColor;
layout( set = 1, binding = 1 ) uniform sampler2D uMtlColor;
layout( set = 1, binding = 2 ) uniform sampler2D uRogColor;
layout( set = 1, binding = 3 ) uniform sampler2D uNorColor;
layout( set = 1, binding = 4 ) uniform sampler2D uAlpColor;

layout( location = 0 ) out vec4 oColor;

const float PI = 3.14159265359;

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

float beckmannDistribution(float NdotH, float alpha) {
    float alpha2 = alpha * alpha;
    float NdotH2 = NdotH * NdotH;
    float exponent = ((NdotH2 - 1.0) / (alpha2 * NdotH2));
    float e = exp(exponent);
    float denom = PI * alpha2 * NdotH2 * NdotH2;
    float D = e / denom;
    return max(D, 0.0); 
}

float geometryCookTorrance(vec3 N, vec3 V, vec3 L, vec3 H) {
    float a = 2 * ( (max(dot(N, H), 0.0) * max(dot(N, V), 0.0)) / (1e-12 + dot(V, H)) );
    float b = 2 * ( (max(dot(N, H), 0.0) * max(dot(N, L), 0.0)) / (1e-12 + dot(V, H)) );

    float res = min(1, min(a,b));
    return res;
}

void main()
{
    // 1.1
	// vec3 normalColor = fragNormal;
    // vec3 viewDirColor = viewDir;
    // vec3 lightDirColor = lightDir;
    // oColor = vec4(lightDirColor, 1.0);

    //vec3 N = fragNormal;
    vec3 N = normalize(texture(uNorColor, fragTexCoord).rgb * 2.0 - 1.0);
    N = normalize(vTBN * N);
    vec3 V = normalize(viewDir);
    vec3 L = normalize(lightDir);
    vec3 H = normalize(V + L);

    vec3 baseColor = texture(uTexColor, fragTexCoord).rgb;
    float metalness = texture(uMtlColor, fragTexCoord).r;
    float roughness = texture(uRogColor, fragTexCoord).r;

    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float NdotH = max(dot(N, H), 0.0);

    vec3 F0 = mix(vec3(0.04), baseColor, metalness);

    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
    float D = beckmannDistribution(NdotH, roughness);
    float G = geometryCookTorrance(N, V, L, H);

    vec3 diffuse = (baseColor / PI) * (vec3(1.0) - F) * (1.0 - metalness);
    vec3 specular = (D * F * G) / (4.0 * NdotV * NdotL);

    vec3 ambient = 0.01 * baseColor;
    vec3 color = ambient + (diffuse + specular) * lightCol * NdotL;

    oColor = vec4(color, 1.0);
}
