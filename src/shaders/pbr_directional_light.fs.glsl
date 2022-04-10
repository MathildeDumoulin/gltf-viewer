#version 330

in vec3 vViewSpaceNormal;
in vec2 vTexCoords;

uniform vec3 uLightDirection;
uniform vec3 uLightIntensity;

uniform vec4 uBaseColorFactor;
uniform sampler2D uBaseColorTexture;

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
  vec3 L = uLightDirection;

  vec4 baseColorFromTexture = SRGBtoLINEAR(texture(uBaseColorTexture, vTexCoords));
  vec4 baseColor = uBaseColorFactor * baseColorFromTexture;
  float NdotL = clamp(dot(N, L), 0., 1.);
  vec3 diffuse = baseColor.rgb * M_1_PI;

  fColor = LINEARtoSRGB(diffuse * uLightIntensity * NdotL);
}