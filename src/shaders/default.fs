#version 410 core
out vec4 FragColor;

in VS_OUT
{
    vec3 FragPos;
    vec3 Normal;
    vec2 TexCoords;
    vec3 lightDir;
    vec3 color;
}
fs_in;

uniform vec3 lightCol;
uniform float lightIntensity;
uniform vec3 viewPos;

struct Material
{
    sampler2D diffuse1;
    sampler2D properties1;
};

uniform Material material;

const float ambientLight = 0.8;

void main()
{
    // Sample textures
    vec3 albedo = texture(material.diffuse1, fs_in.TexCoords).rgb;

    float roughness = texture(material.properties1, fs_in.TexCoords).r;
    float specular = texture(material.properties1, fs_in.TexCoords).g;

    vec3 diffuse = dot(fs_in.Normal, fs_in.lightDir) * lightCol * lightIntensity;

    vec3 outputColor = albedo * (ambientLight + 0.5 * diffuse);

    FragColor = vec4(outputColor, 1.0);  // Output final color

    // FragColor = vec4(fs_in.color, 1);
}
