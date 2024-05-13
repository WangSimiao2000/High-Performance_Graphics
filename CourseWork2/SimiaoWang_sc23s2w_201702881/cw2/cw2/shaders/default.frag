#version 450
#extension GL_KHR_vulkan_glsl : enable

layout( location = 0 ) in vec2 v2fTexCoord;
layout( location = 1 ) in vec3 v2fNormal;
layout( location = 2 ) in vec3 v2fViewDir;
layout( location = 3 ) in vec3 v2fLightDir;
layout( location = 4 ) in vec3 v2fLightCol;
//layout( location = 5 ) in mat3 vTBN;

layout( set = 1, binding = 0 ) uniform sampler2D uTexColor;
layout( set = 1, binding = 1 ) uniform sampler2D uMtlColor;
layout( set = 1, binding = 2 ) uniform sampler2D uRogColor;
layout( set = 1, binding = 3 ) uniform sampler2D uNorColor;
layout( location = 0 ) out vec4 oColor;

const float PI = 3.1416;

vec3 BRDF(vec3 diffuseColor, float BeckmannFunc, vec3 FresnelFunc, float CookTorranceFunc , float NdotV, float NdotL){
    vec3 Numerator = BeckmannFunc * FresnelFunc * CookTorranceFunc;
    float Denominator = 4 * NdotV * NdotL;
    return (diffuseColor + (Numerator/Denominator));
}

float BeckmannFunc(float NdotH, float alpha) {
    float alpha2 = alpha * alpha;
    float NdotH2 = NdotH * NdotH;
    float tanThetaH2 = (1.0 - NdotH2) / NdotH2;
    float exponent = -tanThetaH2 / alpha2;
    float e = exp(exponent);
    float denom = PI * alpha2 * NdotH2 * NdotH2;
    float D = e / denom;
    return max(D, 0.0); 
}

vec3 SpecularBaseReflectivity(float mtl, vec3 baseColor)
{
    vec3 a = (1 - mtl) * vec3(0.04);
    vec3 b = mtl * baseColor;
    vec3 result = a+b;
    return result;

}

vec3 FresnelFunc(vec3 SpecBaseRefColor, float VdotH) {
    return SpecBaseRefColor + (1.0 - SpecBaseRefColor) * pow(1.0 - VdotH, 5.0);
}


float CookTorranceFunc(float NdotV, float NdotL, float NdotH, float VdotH)
{
    float a = 2 * ( (max(NdotH, 0.0) * max(NdotV, 0.0)) / (VdotH) );
    float b = 2 * ( (max(NdotH, 0.0) * max(NdotL, 0.0)) / (VdotH) );

    float res = min(1, min(a,b));
    return res;
}

void main()
{
    // -- 1.1 START -- //
    vec3 normalColor = v2fNormal;
    vec3 viewDirColor = v2fViewDir;
    vec3 lightDirColor = v2fLightDir;
    
    //oColor = vec4(normalColor, 1.0);
    //oColor = vec4(viewDirColor, 1.0);
    //oColor = vec4(lightDirColor, 1.0);
    // -- 1.1  END  -- //

    
    // -- 1.2 START -- //
    vec3 N = normalize(v2fNormal); // Surface normal
    vec3 L = normalize(v2fLightDir); // Light direction (normalized), pointing towards the light
    vec3 V = normalize(v2fViewDir); // View direction (normalized), pointing towards the camera/viewer
    vec3 H = normalize(L + V); // Half vector (normalized), computed from the light and view directions

    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    
    vec3 albedo = texture(uTexColor, v2fTexCoord).rgb;
    float metalness = texture(uMtlColor, v2fTexCoord).r;
    float roughness = texture(uRogColor, v2fTexCoord).r;   

    //float BeckmannResult = BeckmannFunc(NdotH, roughness);
    float BeckmannResult = BeckmannFunc(NdotH, roughness*roughness);
    vec3 SpecBaseRefColor = SpecularBaseReflectivity(metalness, albedo);
    vec3 FresnelColor = FresnelFunc(SpecBaseRefColor, VdotH);
    float CookTorranceResult = CookTorranceFunc(NdotV, NdotL, NdotH, VdotH);

    vec3 diffuseColor = (albedo / PI) * (vec3(1.0) - FresnelColor) * (1.0 - metalness);
    vec3 BRDFColor = BRDF(diffuseColor, BeckmannResult, FresnelColor, CookTorranceResult, NdotV, NdotL);
    vec3 ambientColor = 0.02 * albedo;
    vec3 lighting = ambientColor + (BRDFColor) * NdotL;    
    vec3 pbrColor = lighting * v2fLightCol;

    //oColor = vec4(vec3(BeckmannResult), 1.0);         // D
    //oColor = vec4(vec3(CookTorranceResult), 1.0);     // G
    //oColor = vec4(FresnelColor, 1.0);                 // F
    //oColor = vec4(BRDFColor-diffuseColor, 1.0);       // PBR specular term
    oColor = vec4(pbrColor, 1.0);
    // -- 1.2  END  -- //
}