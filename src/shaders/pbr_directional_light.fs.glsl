#version 330

in vec3 vViewSpacePosition;
in vec3 vViewSpaceNormal;
in vec2 vTexCoords;

uniform vec3 uLightDirection;
uniform vec3 uLightIntensity;

uniform vec4 uBaseColorFactor;
uniform float uMetallicFactor;
uniform float uRoughnessFactor;
uniform vec3 uEmissiveFactor;
uniform float uOcclusionStrength;

uniform sampler2D uBaseColorTexture;
uniform sampler2D uMetallicRoughnessTexture;
uniform sampler2D uEmissiveTexture;
uniform sampler2D uOcclusionTexture;
uniform int uApplyOcclusion;



out vec3 fColor;

// Constants
const float GAMMA = 2.2;
const float INV_GAMMA = 1. / GAMMA;
const float M_PI = 3.141592653589793;
const float M_1_PI = 1.0 / M_PI;

vec3 LINEARtoSRGB(vec3 color) { return pow(color, vec3(INV_GAMMA)); }
vec4 SRGBtoLINEAR(vec4 srgbIn)
{
  return vec4(pow(srgbIn.xyz, vec3(GAMMA)), srgbIn.w);
}

void main()
{
  vec3 N = normalize(vViewSpaceNormal);
  vec3 V = normalize(-vViewSpacePosition);
  vec3 L = uLightDirection;
  vec3 H = normalize(L + V);

  vec4 baseColorFromTexture = SRGBtoLINEAR(texture(uBaseColorTexture, vTexCoords));
  vec4 baseColor = uBaseColorFactor * baseColorFromTexture;
  vec4 metallicRoughnessFromTexture = texture(uMetallicRoughnessTexture, vTexCoords);


  vec3 metallic = vec3(uMetallicFactor * metallicRoughnessFromTexture.b);
  float roughness = uRoughnessFactor * metallicRoughnessFromTexture.g;

  vec3 dielectricSpecular = vec3(0.04);
  vec3 black = vec3(0.);

  vec3 c_diff =
      mix(baseColor.rgb * (1 - dielectricSpecular.r), black, metallic);
  vec3 F_0 = mix(vec3(dielectricSpecular), baseColor.rgb, metallic);
  float alpha = roughness * roughness;

  float VdotH = clamp(dot(V, H), 0., 1.);
  float baseShlickFactor = 1 - VdotH;
  float shlickFactor = baseShlickFactor * baseShlickFactor; // power 2
  shlickFactor *= shlickFactor;                             // power 4
  shlickFactor *= baseShlickFactor;                         // power 5
  vec3 F = F_0 + (vec3(1) - F_0) * shlickFactor;

  float sqrAlpha = alpha * alpha;
  float NdotL = clamp(dot(N, L), 0., 1.);
  float NdotV = clamp(dot(N, V), 0., 1.);
  float visDenominator =
      NdotL * sqrt(NdotV * NdotV * (1 - sqrAlpha) + sqrAlpha) +
      NdotV * sqrt(NdotL * NdotL * (1 - sqrAlpha) + sqrAlpha);
  float Vis = visDenominator > 0. ? 0.5 / visDenominator : 0.0;

  float NdotH = clamp(dot(N, H), 0., 1.);
  float baseDenomD = (NdotH * NdotH * (sqrAlpha - 1.) + 1.);
  float D = M_1_PI * sqrAlpha / (baseDenomD * baseDenomD);

  vec3 f_specular = F * Vis * D;

  vec3 diffuse = c_diff * M_1_PI;

  vec3 f_diffuse = (1. - F) * diffuse;
  vec3 emissive = SRGBtoLINEAR(texture2D(uEmissiveTexture, vTexCoords)).rgb * uEmissiveFactor;
  vec3 color = (f_diffuse + f_specular) * uLightIntensity * NdotL + emissive;

  if (uApplyOcclusion == 1) {
    float ao = texture2D(uOcclusionTexture, vTexCoords).r;
    color = mix(color, color * ao, uOcclusionStrength);
  }

  fColor = LINEARtoSRGB(color);
}