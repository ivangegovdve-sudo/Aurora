void setupMaterial_f9fe6c76b2284bbb(
	Material_f9fe6c76b2284bbb material,
	out float3 base_color,
	out float3 specular_color,
	out float specular_roughness,
	out float specular_IOR,
	out float coat,
	out float coat_roughness,
	out float3 emission_color)
{
	// Graph input SS_ShaderRef1/base_color
	//{
float3 SS_ShaderRef1_base_color//};
 = material.SS_ShaderRef1_base_color;
	base_color = SS_ShaderRef1_base_color;// Output connection
	// Graph input SS_ShaderRef1/specular_color
	//{
float3 SS_ShaderRef1_specular_color//};
 = material.SS_ShaderRef1_specular_color;
	specular_color = SS_ShaderRef1_specular_color;// Output connection
	// Graph input SS_ShaderRef1/specular_roughness
	//{
float SS_ShaderRef1_specular_roughness//};
 = material.SS_ShaderRef1_specular_roughness;
	specular_roughness = SS_ShaderRef1_specular_roughness;// Output connection
	// Graph input SS_ShaderRef1/specular_IOR
	//{
float SS_ShaderRef1_specular_IOR//};
 = material.SS_ShaderRef1_specular_IOR;
	specular_IOR = SS_ShaderRef1_specular_IOR;// Output connection
	// Graph input SS_ShaderRef1/coat
	//{
float SS_ShaderRef1_coat//};
 = material.SS_ShaderRef1_coat;
	coat = SS_ShaderRef1_coat;// Output connection
	// Graph input SS_ShaderRef1/coat_roughness
	//{
float SS_ShaderRef1_coat_roughness//};
 = material.SS_ShaderRef1_coat_roughness;
	coat_roughness = SS_ShaderRef1_coat_roughness;// Output connection
	// Graph input SS_ShaderRef1/emission_color
	//{
float3 SS_ShaderRef1_emission_color//};
 = material.SS_ShaderRef1_emission_color;
	emission_color = SS_ShaderRef1_emission_color;// Output connection
}
