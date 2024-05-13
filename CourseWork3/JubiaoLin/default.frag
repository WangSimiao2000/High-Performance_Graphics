/*
#version 450

layout( location = 0 ) in vec2 v2fTexCoord;
layout( location = 1 ) in vec3 v2fNormal;
layout( location = 2 ) in vec3 v2fViewDir;
layout( location = 3 ) in vec3 v2fLightDir;
layout( location = 4 ) in vec3 v2fLightCol;

layout( set = 1, binding = 0 ) uniform sampler2D uTexColor;
layout( set = 1, binding = 1 ) uniform sampler2D uMtlColor;
layout( set = 1, binding = 2 ) uniform sampler2D uRogColor;
layout( set = 1, binding = 3 ) uniform sampler2D uNorColor;
layout( location = 0 ) out vec4 oColor;

const float PI = 3.14159265359;

vec3 BRDF(float fun_d, vec3 F, float cook, float NdotV, float NdotL, vec3 dif){
    vec3 a = fun_d * F * cook;
    float b = 4 * max(NdotV, 0.0) * max(NdotL, 0.0);

    return (dif + a/b);

}

float Fun_D(float NdotH, float alpha) {
    float alpha2 = alpha * alpha;
    float NdotH2 = NdotH * NdotH;
    float exponent = ((NdotH2 - 1.0) / (alpha2 * NdotH2));
    float e = exp(exponent);
    float denom = PI * alpha2 * NdotH2 * NdotH2;
    float D = e / denom;
    return max(D, 0.0); 
}


vec3 Fun_F0(float mtl, vec3 baseColor)
{
    vec3 a = (1 - mtl) * vec3(0.04);
    vec3 b = mtl * baseColor;
    vec3 result = a+b;
    return result;

}

//vec3 Fresnel(vec3 result_f0, float LdotH, float H)
//{
//   float HdotV = dot(H, LdotH);
//   float a = pow((1 - HdotV), 5);
//   vec3 res = result_f0 + (1 - result_f0) * a;
//    return res;
//}

vec3 Fresnel(vec3 F0, float VdotH) {
    return F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
}


float Cook(float NdotV, float NdotL, float NdotH, float VdotH)
{
    float a = 2 * ( (max(NdotH, 0.0) * max(NdotV, 0.0)) / (VdotH) );
    float b = 2 * ( (max(NdotH, 0.0) * max(NdotL, 0.0)) / (VdotH) );

    float res = min(1, min(a,b));
    return res;
}

void main()
{
// ------------ 1.3 ----------------
    vec4 alphaMasking = texture(uTexColor, v2fTexCoord);
    if(alphaMasking.a < 0.5){
        discard;
    }

 

 // ---------  1.1 ------------
    vec3 normalColor = v2fNormal;
    vec3 viewDirColor = v2fViewDir;
    vec3 lightDirColor = v2fLightDir ;

    //oColor.rgb = normalColor;
    //oColor.rgb = viewDirColor;
    //oColor.rgb = lightDirColor; 
    //oColor = vec4(normalColor, 1.0);
    
    // ------------ 1.2 ----------------
    
    
    vec3 albedo = texture(uTexColor, v2fTexCoord).rgb;
    float metalness = texture(uMtlColor, v2fTexCoord).r;                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   
    float roughness = texture(uRogColor, v2fTexCoord).r;

    vec3 N = normalize(v2fNormal);
    vec3 V = normalize(v2fViewDir); 
    vec3 L = normalize(v2fLightDir);
    vec3 H = normalize(L + V);

    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);



    float D = Fun_D(NdotH, roughness);
    vec3 F0 = Fun_F0(metalness, albedo);
    vec3 F = Fresnel(F0, VdotH);
    float G = Cook(NdotV, NdotL, NdotH, VdotH);

    vec3 diffuse = (albedo / PI) * (vec3(1.0) - F) * (1.0 - metalness);  
    vec3 specular = D * F * G / (4.0 * NdotV * NdotL);
    vec3 ambient = 0.02 * albedo;
    vec3 lighting = ambient + (diffuse + specular) * NdotL;
    
    // pbr model 
    vec3 pbrModel = lighting * v2fLightCol;
    //oColor = vec4(pbrModel, 1.0);

    // Beckmann -- function D
    //oColor = vec4(vec3(D), 1.0);

    // Cook-Torrance --function G
    //oColor = vec4(vec3(G), 1.0);
    
    // Fresnel -- function F
    //oColor = vec4(F, 1.0);

    //PBR specular term
    vec3 resCol = specular * v2fLightCol;
    //oColor = vec4(resCol, 1.0);



    // ------------ NORMAL OUTPUT -----------
	oColor = vec4( texture( uTexColor, v2fTexCoord ).rgb, 1.f );
	
}
*/
#version 450

layout( location = 0 ) in vec2 v2fTexCoord;
layout( location = 1 ) in vec3 v2fNormal;
layout( location = 2 ) in vec3 v2fViewDir;
layout( location = 3 ) in vec3 v2fLightDir;
layout( location = 4 ) in vec3 v2fLightCol;
layout( location = 5 ) in vec3 v2fPosition;

layout( set = 1, binding = 0 ) uniform sampler2D uTexColor;
layout( set = 1, binding = 1 ) uniform sampler2D uMtlColor;
layout( set = 1, binding = 2 ) uniform sampler2D uRogColor;


layout( location = 0) out vec3 gPosition;
layout( location = 1) out vec3 gNormal;
layout( location = 2) out vec3 gTexColor;
layout( location = 3) out vec3 gMtlColor;
layout( location = 4) out vec3 gRogColor;


void main(){
    gPosition = v2fPosition;
    gNormal = v2fNormal;
    gTexColor = texture(uTexColor, v2fTexCoord).rgb;
    gMtlColor = texture(uMtlColor, v2fTexCoord).rgb;
    gRogColor = texture(uRogColor, v2fTexCoord).rgb;

}