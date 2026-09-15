void setupMaterial_2eb268755fcd9b9f(
	Material_2eb268755fcd9b9f material,
	sampler2D basecolor_bitmap_image_parameter,
	sampler2D opacity_bitmap_image_parameter,
	out float base,
	out float3 base_color,
	out float metalness,
	out float specular,
	out float specular_roughness,
	out float specular_IOR,
	out float3 opacity,
	out bool thin_walled)
{
	// Graph input TestMaskWithChromeKeyDecal/base
	//{
float TestMaskWithChromeKeyDecal_base//};
 = material.TestMaskWithChromeKeyDecal_base;
	base = TestMaskWithChromeKeyDecal_base;// Output connection
	//Temp input variables for basecolor_bitmap 
	float3 nodeOutTmp_basecolor_bitmap_out; //Temp output variable for out 
	SamplerTexture2D nodeTmp_basecolor_bitmap_file; //Temp input variable for file 
	float2 nodeTmp_basecolor_bitmap_realworld_offset; //Temp input variable for realworld_offset 
	float2 nodeTmp_basecolor_bitmap_realworld_scale; //Temp input variable for realworld_scale 
	float2 nodeTmp_basecolor_bitmap_uv_offset; //Temp input variable for uv_offset 
	float2 nodeTmp_basecolor_bitmap_uv_scale; //Temp input variable for uv_scale 
	float nodeTmp_basecolor_bitmap_rotation_angle; //Temp input variable for rotation_angle 
	float nodeTmp_basecolor_bitmap_rgbamount; //Temp input variable for rgbamount 
	bool nodeTmp_basecolor_bitmap_invert; //Temp input variable for invert 
	int nodeTmp_basecolor_bitmap_uaddressmode; //Temp input variable for uaddressmode 
	int nodeTmp_basecolor_bitmap_vaddressmode; //Temp input variable for vaddressmode 
	float2 nodeTmp_basecolor_bitmap_texcoord; //Temp input variable for texcoord 
	// Graph input TestMaskWithChromeKeyDecal_nodegraph/basecolor_bitmap/file
	//{
SamplerTexture2D basecolor_bitmap_file1//};
 = basecolor_bitmap_image_parameter;
	nodeTmp_basecolor_bitmap_file = basecolor_bitmap_file1;// Output connection
	//Temp input variables for basecolor_bitmap_realworld_offset_unit 
	float2 nodeOutTmp_basecolor_bitmap_realworld_offset_unit_out; //Temp output variable for out 
	float2 nodeTmp_basecolor_bitmap_realworld_offset_unit_in1; //Temp input variable for in1 
	float nodeTmp_basecolor_bitmap_realworld_offset_unit_in2; //Temp input variable for in2 
	// Graph input TestMaskWithChromeKeyDecal_nodegraph/basecolor_bitmap/realworld_offset
	//{
float2 basecolor_bitmap_realworld_offset_unit_in11//};
 = material.TestMaskWithChromeKeyDecal_nodegraph_basecolor_bitmap_realworld_offset;
	nodeTmp_basecolor_bitmap_realworld_offset_unit_in1 = basecolor_bitmap_realworld_offset_unit_in11;// Output connection
	// Graph input 
	//{
float basecolor_bitmap_realworld_offset_unit_in21 = 1//};
;
	nodeTmp_basecolor_bitmap_realworld_offset_unit_in2 = basecolor_bitmap_realworld_offset_unit_in21;// Output connection
	// Graph input function call realworld_offset (See definition IM_multiply_vector2FA_genglsl)
{
	//{
    float2 basecolor_bitmap_realworld_offset_unit_out = nodeTmp_basecolor_bitmap_realworld_offset_unit_in1 * nodeTmp_basecolor_bitmap_realworld_offset_unit_in2;
//};
	nodeOutTmp_basecolor_bitmap_realworld_offset_unit_out = basecolor_bitmap_realworld_offset_unit_out;// Output connection
	nodeTmp_basecolor_bitmap_realworld_offset = basecolor_bitmap_realworld_offset_unit_out;// Output connection
}
	//Temp input variables for basecolor_bitmap_realworld_scale_unit 
	float2 nodeOutTmp_basecolor_bitmap_realworld_scale_unit_out; //Temp output variable for out 
	float2 nodeTmp_basecolor_bitmap_realworld_scale_unit_in1; //Temp input variable for in1 
	float nodeTmp_basecolor_bitmap_realworld_scale_unit_in2; //Temp input variable for in2 
	// Graph input TestMaskWithChromeKeyDecal_nodegraph/basecolor_bitmap/realworld_scale
	//{
float2 basecolor_bitmap_realworld_scale_unit_in11//};
 = material.TestMaskWithChromeKeyDecal_nodegraph_basecolor_bitmap_realworld_scale;
	nodeTmp_basecolor_bitmap_realworld_scale_unit_in1 = basecolor_bitmap_realworld_scale_unit_in11;// Output connection
	// Graph input 
	//{
float basecolor_bitmap_realworld_scale_unit_in21 = 1//};
;
	nodeTmp_basecolor_bitmap_realworld_scale_unit_in2 = basecolor_bitmap_realworld_scale_unit_in21;// Output connection
	// Graph input function call realworld_scale (See definition IM_multiply_vector2FA_genglsl)
{
	//{
    float2 basecolor_bitmap_realworld_scale_unit_out = nodeTmp_basecolor_bitmap_realworld_scale_unit_in1 * nodeTmp_basecolor_bitmap_realworld_scale_unit_in2;
//};
	nodeOutTmp_basecolor_bitmap_realworld_scale_unit_out = basecolor_bitmap_realworld_scale_unit_out;// Output connection
	nodeTmp_basecolor_bitmap_realworld_scale = basecolor_bitmap_realworld_scale_unit_out;// Output connection
}
	//Temp input variables for basecolor_bitmap_uv_offset_unit 
	float2 nodeOutTmp_basecolor_bitmap_uv_offset_unit_out; //Temp output variable for out 
	float2 nodeTmp_basecolor_bitmap_uv_offset_unit_in1; //Temp input variable for in1 
	float nodeTmp_basecolor_bitmap_uv_offset_unit_in2; //Temp input variable for in2 
	// Graph input TestMaskWithChromeKeyDecal_nodegraph/basecolor_bitmap/uv_offset
	//{
float2 basecolor_bitmap_uv_offset_unit_in11//};
 = material.TestMaskWithChromeKeyDecal_nodegraph_basecolor_bitmap_uv_offset;
	nodeTmp_basecolor_bitmap_uv_offset_unit_in1 = basecolor_bitmap_uv_offset_unit_in11;// Output connection
	// Graph input 
	//{
float basecolor_bitmap_uv_offset_unit_in21 = 1//};
;
	nodeTmp_basecolor_bitmap_uv_offset_unit_in2 = basecolor_bitmap_uv_offset_unit_in21;// Output connection
	// Graph input function call uv_offset (See definition IM_multiply_vector2FA_genglsl)
{
	//{
    float2 basecolor_bitmap_uv_offset_unit_out = nodeTmp_basecolor_bitmap_uv_offset_unit_in1 * nodeTmp_basecolor_bitmap_uv_offset_unit_in2;
//};
	nodeOutTmp_basecolor_bitmap_uv_offset_unit_out = basecolor_bitmap_uv_offset_unit_out;// Output connection
	nodeTmp_basecolor_bitmap_uv_offset = basecolor_bitmap_uv_offset_unit_out;// Output connection
}
	//Temp input variables for basecolor_bitmap_uv_scale_unit 
	float2 nodeOutTmp_basecolor_bitmap_uv_scale_unit_out; //Temp output variable for out 
	float2 nodeTmp_basecolor_bitmap_uv_scale_unit_in1; //Temp input variable for in1 
	float nodeTmp_basecolor_bitmap_uv_scale_unit_in2; //Temp input variable for in2 
	// Graph input TestMaskWithChromeKeyDecal_nodegraph/basecolor_bitmap/uv_scale
	//{
float2 basecolor_bitmap_uv_scale_unit_in11//};
 = material.TestMaskWithChromeKeyDecal_nodegraph_basecolor_bitmap_uv_scale;
	nodeTmp_basecolor_bitmap_uv_scale_unit_in1 = basecolor_bitmap_uv_scale_unit_in11;// Output connection
	// Graph input 
	//{
float basecolor_bitmap_uv_scale_unit_in21 = 1//};
;
	nodeTmp_basecolor_bitmap_uv_scale_unit_in2 = basecolor_bitmap_uv_scale_unit_in21;// Output connection
	// Graph input function call uv_scale (See definition IM_multiply_vector2FA_genglsl)
{
	//{
    float2 basecolor_bitmap_uv_scale_unit_out = nodeTmp_basecolor_bitmap_uv_scale_unit_in1 * nodeTmp_basecolor_bitmap_uv_scale_unit_in2;
//};
	nodeOutTmp_basecolor_bitmap_uv_scale_unit_out = basecolor_bitmap_uv_scale_unit_out;// Output connection
	nodeTmp_basecolor_bitmap_uv_scale = basecolor_bitmap_uv_scale_unit_out;// Output connection
}
	// Graph input TestMaskWithChromeKeyDecal_nodegraph/basecolor_bitmap/rotation_angle
	//{
float basecolor_bitmap_rotation_angle1//};
 = material.TestMaskWithChromeKeyDecal_nodegraph_basecolor_bitmap_rotation_angle;
	nodeTmp_basecolor_bitmap_rotation_angle = basecolor_bitmap_rotation_angle1;// Output connection
	// Graph input TestMaskWithChromeKeyDecal_nodegraph/basecolor_bitmap/rgbamount
	//{
float basecolor_bitmap_rgbamount1 = 1//};
;
	nodeTmp_basecolor_bitmap_rgbamount = basecolor_bitmap_rgbamount1;// Output connection
	// Graph input TestMaskWithChromeKeyDecal_nodegraph/basecolor_bitmap/invert
	//{
bool basecolor_bitmap_invert1 = false//};
;
	nodeTmp_basecolor_bitmap_invert = basecolor_bitmap_invert1;// Output connection
	// Graph input TestMaskWithChromeKeyDecal_nodegraph/basecolor_bitmap/uaddressmode
	//{
int basecolor_bitmap_uaddressmode1 = 1//};
;
	nodeTmp_basecolor_bitmap_uaddressmode = basecolor_bitmap_uaddressmode1;// Output connection
	// Graph input TestMaskWithChromeKeyDecal_nodegraph/basecolor_bitmap/vaddressmode
	//{
int basecolor_bitmap_vaddressmode1 = 1//};
;
	nodeTmp_basecolor_bitmap_vaddressmode = basecolor_bitmap_vaddressmode1;// Output connection
	//Temp input variables for geomprop_UV0 
	float2 nodeOutTmp_geomprop_UV0_out; //Temp output variable for out 
	int nodeTmp_geomprop_UV0_index; //Temp input variable for index 
	// Graph input UV0
	//{
int geomprop_UV0_index1 = 0//};
;
	nodeTmp_geomprop_UV0_index = geomprop_UV0_index1;// Output connection
	// Graph input function call texcoord (See definition IM_texcoord_vector2_genslang)
{
	//{
    float2 geomprop_UV0_out1 = vertexData.texCoord.xy;
//};
	nodeOutTmp_geomprop_UV0_out = geomprop_UV0_out1;// Output connection
	nodeTmp_basecolor_bitmap_texcoord = geomprop_UV0_out1;// Output connection
}
	// Graph input function call base_color (See definition adsk:NG_adsk_bitmap_color3)
{
	//{
    float3 basecolor_bitmap_out = float3(0.0);
    adsk_NG_adsk_bitmap_color3(nodeTmp_basecolor_bitmap_file, nodeTmp_basecolor_bitmap_realworld_offset, nodeTmp_basecolor_bitmap_realworld_scale, nodeTmp_basecolor_bitmap_uv_offset, nodeTmp_basecolor_bitmap_uv_scale, nodeTmp_basecolor_bitmap_rotation_angle, nodeTmp_basecolor_bitmap_rgbamount, nodeTmp_basecolor_bitmap_invert, nodeTmp_basecolor_bitmap_uaddressmode, nodeTmp_basecolor_bitmap_vaddressmode, nodeTmp_basecolor_bitmap_texcoord, basecolor_bitmap_out);
//};
	nodeOutTmp_basecolor_bitmap_out = basecolor_bitmap_out;// Output connection
	base_color = basecolor_bitmap_out;// Output connection
}
	// Graph input TestMaskWithChromeKeyDecal/metalness
	//{
float TestMaskWithChromeKeyDecal_metalness//};
 = material.TestMaskWithChromeKeyDecal_metalness;
	metalness = TestMaskWithChromeKeyDecal_metalness;// Output connection
	// Graph input TestMaskWithChromeKeyDecal/specular
	//{
float TestMaskWithChromeKeyDecal_specular//};
 = material.TestMaskWithChromeKeyDecal_specular;
	specular = TestMaskWithChromeKeyDecal_specular;// Output connection
	// Graph input TestMaskWithChromeKeyDecal/specular_roughness
	//{
float TestMaskWithChromeKeyDecal_specular_roughness//};
 = material.TestMaskWithChromeKeyDecal_specular_roughness;
	specular_roughness = TestMaskWithChromeKeyDecal_specular_roughness;// Output connection
	// Graph input TestMaskWithChromeKeyDecal/specular_IOR
	//{
float TestMaskWithChromeKeyDecal_specular_IOR//};
 = material.TestMaskWithChromeKeyDecal_specular_IOR;
	specular_IOR = TestMaskWithChromeKeyDecal_specular_IOR;// Output connection
	//Temp input variables for convert_to_opacity_white_black 
	float3 nodeOutTmp_convert_to_opacity_white_black_out; //Temp output variable for out 
	float nodeTmp_convert_to_opacity_white_black_value1; //Temp input variable for value1 
	float nodeTmp_convert_to_opacity_white_black_value2; //Temp input variable for value2 
	float3 nodeTmp_convert_to_opacity_white_black_in1; //Temp input variable for in1 
	float3 nodeTmp_convert_to_opacity_white_black_in2; //Temp input variable for in2 
	//Temp input variables for magnitude_vector3 
	float nodeOutTmp_magnitude_vector3_out; //Temp output variable for out 
	float3 nodeTmp_magnitude_vector3_in; //Temp input variable for in 
	//Temp input variables for convert_color3_to_vector3 
	float3 nodeOutTmp_convert_color3_to_vector3_out; //Temp output variable for out 
	float3 nodeTmp_convert_color3_to_vector3_in; //Temp input variable for in 
	//Temp input variables for difference_image_with_chromekey 
	float3 nodeOutTmp_difference_image_with_chromekey_out; //Temp output variable for out 
	float3 nodeTmp_difference_image_with_chromekey_fg; //Temp input variable for fg 
	float3 nodeTmp_difference_image_with_chromekey_bg; //Temp input variable for bg 
	float nodeTmp_difference_image_with_chromekey_mix; //Temp input variable for mix 
	//Temp input variables for opacity_bitmap 
	float3 nodeOutTmp_opacity_bitmap_out; //Temp output variable for out 
	SamplerTexture2D nodeTmp_opacity_bitmap_file; //Temp input variable for file 
	float2 nodeTmp_opacity_bitmap_realworld_offset; //Temp input variable for realworld_offset 
	float2 nodeTmp_opacity_bitmap_realworld_scale; //Temp input variable for realworld_scale 
	float2 nodeTmp_opacity_bitmap_uv_offset; //Temp input variable for uv_offset 
	float2 nodeTmp_opacity_bitmap_uv_scale; //Temp input variable for uv_scale 
	float nodeTmp_opacity_bitmap_rotation_angle; //Temp input variable for rotation_angle 
	float nodeTmp_opacity_bitmap_rgbamount; //Temp input variable for rgbamount 
	bool nodeTmp_opacity_bitmap_invert; //Temp input variable for invert 
	int nodeTmp_opacity_bitmap_uaddressmode; //Temp input variable for uaddressmode 
	int nodeTmp_opacity_bitmap_vaddressmode; //Temp input variable for vaddressmode 
	float2 nodeTmp_opacity_bitmap_texcoord; //Temp input variable for texcoord 
	// Graph input TestMaskWithChromeKeyDecal_nodegraph/opacity_bitmap/file
	//{
SamplerTexture2D opacity_bitmap_file1//};
 = opacity_bitmap_image_parameter;
	nodeTmp_opacity_bitmap_file = opacity_bitmap_file1;// Output connection
	//Temp input variables for opacity_bitmap_realworld_offset_unit 
	float2 nodeOutTmp_opacity_bitmap_realworld_offset_unit_out; //Temp output variable for out 
	float2 nodeTmp_opacity_bitmap_realworld_offset_unit_in1; //Temp input variable for in1 
	float nodeTmp_opacity_bitmap_realworld_offset_unit_in2; //Temp input variable for in2 
	// Graph input TestMaskWithChromeKeyDecal_nodegraph/opacity_bitmap/realworld_offset
	//{
float2 opacity_bitmap_realworld_offset_unit_in11//};
 = material.TestMaskWithChromeKeyDecal_nodegraph_opacity_bitmap_realworld_offset;
	nodeTmp_opacity_bitmap_realworld_offset_unit_in1 = opacity_bitmap_realworld_offset_unit_in11;// Output connection
	// Graph input 
	//{
float opacity_bitmap_realworld_offset_unit_in21 = 1//};
;
	nodeTmp_opacity_bitmap_realworld_offset_unit_in2 = opacity_bitmap_realworld_offset_unit_in21;// Output connection
	// Graph input function call realworld_offset (See definition IM_multiply_vector2FA_genglsl)
{
	//{
    float2 opacity_bitmap_realworld_offset_unit_out = nodeTmp_opacity_bitmap_realworld_offset_unit_in1 * nodeTmp_opacity_bitmap_realworld_offset_unit_in2;
//};
	nodeOutTmp_opacity_bitmap_realworld_offset_unit_out = opacity_bitmap_realworld_offset_unit_out;// Output connection
	nodeTmp_opacity_bitmap_realworld_offset = opacity_bitmap_realworld_offset_unit_out;// Output connection
}
	//Temp input variables for opacity_bitmap_realworld_scale_unit 
	float2 nodeOutTmp_opacity_bitmap_realworld_scale_unit_out; //Temp output variable for out 
	float2 nodeTmp_opacity_bitmap_realworld_scale_unit_in1; //Temp input variable for in1 
	float nodeTmp_opacity_bitmap_realworld_scale_unit_in2; //Temp input variable for in2 
	// Graph input TestMaskWithChromeKeyDecal_nodegraph/opacity_bitmap/realworld_scale
	//{
float2 opacity_bitmap_realworld_scale_unit_in11//};
 = material.TestMaskWithChromeKeyDecal_nodegraph_opacity_bitmap_realworld_scale;
	nodeTmp_opacity_bitmap_realworld_scale_unit_in1 = opacity_bitmap_realworld_scale_unit_in11;// Output connection
	// Graph input 
	//{
float opacity_bitmap_realworld_scale_unit_in21 = 1//};
;
	nodeTmp_opacity_bitmap_realworld_scale_unit_in2 = opacity_bitmap_realworld_scale_unit_in21;// Output connection
	// Graph input function call realworld_scale (See definition IM_multiply_vector2FA_genglsl)
{
	//{
    float2 opacity_bitmap_realworld_scale_unit_out = nodeTmp_opacity_bitmap_realworld_scale_unit_in1 * nodeTmp_opacity_bitmap_realworld_scale_unit_in2;
//};
	nodeOutTmp_opacity_bitmap_realworld_scale_unit_out = opacity_bitmap_realworld_scale_unit_out;// Output connection
	nodeTmp_opacity_bitmap_realworld_scale = opacity_bitmap_realworld_scale_unit_out;// Output connection
}
	//Temp input variables for opacity_bitmap_uv_offset_unit 
	float2 nodeOutTmp_opacity_bitmap_uv_offset_unit_out; //Temp output variable for out 
	float2 nodeTmp_opacity_bitmap_uv_offset_unit_in1; //Temp input variable for in1 
	float nodeTmp_opacity_bitmap_uv_offset_unit_in2; //Temp input variable for in2 
	// Graph input TestMaskWithChromeKeyDecal_nodegraph/opacity_bitmap/uv_offset
	//{
float2 opacity_bitmap_uv_offset_unit_in11//};
 = material.TestMaskWithChromeKeyDecal_nodegraph_opacity_bitmap_uv_offset;
	nodeTmp_opacity_bitmap_uv_offset_unit_in1 = opacity_bitmap_uv_offset_unit_in11;// Output connection
	// Graph input 
	//{
float opacity_bitmap_uv_offset_unit_in21 = 1//};
;
	nodeTmp_opacity_bitmap_uv_offset_unit_in2 = opacity_bitmap_uv_offset_unit_in21;// Output connection
	// Graph input function call uv_offset (See definition IM_multiply_vector2FA_genglsl)
{
	//{
    float2 opacity_bitmap_uv_offset_unit_out = nodeTmp_opacity_bitmap_uv_offset_unit_in1 * nodeTmp_opacity_bitmap_uv_offset_unit_in2;
//};
	nodeOutTmp_opacity_bitmap_uv_offset_unit_out = opacity_bitmap_uv_offset_unit_out;// Output connection
	nodeTmp_opacity_bitmap_uv_offset = opacity_bitmap_uv_offset_unit_out;// Output connection
}
	//Temp input variables for opacity_bitmap_uv_scale_unit 
	float2 nodeOutTmp_opacity_bitmap_uv_scale_unit_out; //Temp output variable for out 
	float2 nodeTmp_opacity_bitmap_uv_scale_unit_in1; //Temp input variable for in1 
	float nodeTmp_opacity_bitmap_uv_scale_unit_in2; //Temp input variable for in2 
	// Graph input TestMaskWithChromeKeyDecal_nodegraph/opacity_bitmap/uv_scale
	//{
float2 opacity_bitmap_uv_scale_unit_in11//};
 = material.TestMaskWithChromeKeyDecal_nodegraph_opacity_bitmap_uv_scale;
	nodeTmp_opacity_bitmap_uv_scale_unit_in1 = opacity_bitmap_uv_scale_unit_in11;// Output connection
	// Graph input 
	//{
float opacity_bitmap_uv_scale_unit_in21 = 1//};
;
	nodeTmp_opacity_bitmap_uv_scale_unit_in2 = opacity_bitmap_uv_scale_unit_in21;// Output connection
	// Graph input function call uv_scale (See definition IM_multiply_vector2FA_genglsl)
{
	//{
    float2 opacity_bitmap_uv_scale_unit_out = nodeTmp_opacity_bitmap_uv_scale_unit_in1 * nodeTmp_opacity_bitmap_uv_scale_unit_in2;
//};
	nodeOutTmp_opacity_bitmap_uv_scale_unit_out = opacity_bitmap_uv_scale_unit_out;// Output connection
	nodeTmp_opacity_bitmap_uv_scale = opacity_bitmap_uv_scale_unit_out;// Output connection
}
	// Graph input TestMaskWithChromeKeyDecal_nodegraph/opacity_bitmap/rotation_angle
	//{
float opacity_bitmap_rotation_angle1//};
 = material.TestMaskWithChromeKeyDecal_nodegraph_opacity_bitmap_rotation_angle;
	nodeTmp_opacity_bitmap_rotation_angle = opacity_bitmap_rotation_angle1;// Output connection
	// Graph input TestMaskWithChromeKeyDecal_nodegraph/opacity_bitmap/rgbamount
	//{
float opacity_bitmap_rgbamount1 = 1//};
;
	nodeTmp_opacity_bitmap_rgbamount = opacity_bitmap_rgbamount1;// Output connection
	// Graph input TestMaskWithChromeKeyDecal_nodegraph/opacity_bitmap/invert
	//{
bool opacity_bitmap_invert1 = false//};
;
	nodeTmp_opacity_bitmap_invert = opacity_bitmap_invert1;// Output connection
	// Graph input TestMaskWithChromeKeyDecal_nodegraph/opacity_bitmap/uaddressmode
	//{
int opacity_bitmap_uaddressmode1 = 1//};
;
	nodeTmp_opacity_bitmap_uaddressmode = opacity_bitmap_uaddressmode1;// Output connection
	// Graph input TestMaskWithChromeKeyDecal_nodegraph/opacity_bitmap/vaddressmode
	//{
int opacity_bitmap_vaddressmode1 = 1//};
;
	nodeTmp_opacity_bitmap_vaddressmode = opacity_bitmap_vaddressmode1;// Output connection
	nodeTmp_opacity_bitmap_texcoord = nodeOutTmp_geomprop_UV0_out;// Output connection
	// Graph input function call fg (See definition adsk:NG_adsk_bitmap_color3)
{
	//{
    float3 opacity_bitmap_out = float3(0.0);
    adsk_NG_adsk_bitmap_color3(nodeTmp_opacity_bitmap_file, nodeTmp_opacity_bitmap_realworld_offset, nodeTmp_opacity_bitmap_realworld_scale, nodeTmp_opacity_bitmap_uv_offset, nodeTmp_opacity_bitmap_uv_scale, nodeTmp_opacity_bitmap_rotation_angle, nodeTmp_opacity_bitmap_rgbamount, nodeTmp_opacity_bitmap_invert, nodeTmp_opacity_bitmap_uaddressmode, nodeTmp_opacity_bitmap_vaddressmode, nodeTmp_opacity_bitmap_texcoord, opacity_bitmap_out);
//};
	nodeOutTmp_opacity_bitmap_out = opacity_bitmap_out;// Output connection
	nodeTmp_difference_image_with_chromekey_fg = opacity_bitmap_out;// Output connection
}
	// Graph input TestMaskWithChromeKeyDecal_nodegraph/difference_image_with_chromekey/bg
	//{
float3 difference_image_with_chromekey_bg1//};
 = material.TestMaskWithChromeKeyDecal_nodegraph_difference_image_with_chromekey_bg;
	nodeTmp_difference_image_with_chromekey_bg = difference_image_with_chromekey_bg1;// Output connection
	// Graph input TestMaskWithChromeKeyDecal_nodegraph/difference_image_with_chromekey/mix
	//{
float difference_image_with_chromekey_mix1//};
 = material.TestMaskWithChromeKeyDecal_nodegraph_difference_image_with_chromekey_mix;
	nodeTmp_difference_image_with_chromekey_mix = difference_image_with_chromekey_mix1;// Output connection
	// Graph input function call in (See definition IM_difference_color3_genglsl)
{
	//{
    float3 difference_image_with_chromekey_out = (nodeTmp_difference_image_with_chromekey_mix*abs(nodeTmp_difference_image_with_chromekey_bg - nodeTmp_difference_image_with_chromekey_fg)) + ((1.0-nodeTmp_difference_image_with_chromekey_mix)*nodeTmp_difference_image_with_chromekey_bg);
//};
	nodeOutTmp_difference_image_with_chromekey_out = difference_image_with_chromekey_out;// Output connection
	nodeTmp_convert_color3_to_vector3_in = difference_image_with_chromekey_out;// Output connection
}
	// Graph input function call in (See definition NG_convert_color3_vector3)
{
	//{
    float3 convert_color3_to_vector3_out = float3(0.0);
    NG_convert_color3_vector3(nodeTmp_convert_color3_to_vector3_in, convert_color3_to_vector3_out);
//};
	nodeOutTmp_convert_color3_to_vector3_out = convert_color3_to_vector3_out;// Output connection
	nodeTmp_magnitude_vector3_in = convert_color3_to_vector3_out;// Output connection
}
	// Graph input function call value1 (See definition IM_magnitude_vector3_genglsl)
{
	//{
    float magnitude_vector3_out = length(nodeTmp_magnitude_vector3_in);
//};
	nodeOutTmp_magnitude_vector3_out = magnitude_vector3_out;// Output connection
	nodeTmp_convert_to_opacity_white_black_value1 = magnitude_vector3_out;// Output connection
}
	// Graph input TestMaskWithChromeKeyDecal_nodegraph/convert_to_opacity_white_black/value2
	//{
float convert_to_opacity_white_black_value21//};
 = material.TestMaskWithChromeKeyDecal_nodegraph_convert_to_opacity_white_black_value2;
	nodeTmp_convert_to_opacity_white_black_value2 = convert_to_opacity_white_black_value21;// Output connection
	// Graph input TestMaskWithChromeKeyDecal_nodegraph/convert_to_opacity_white_black/in1
	//{
float3 convert_to_opacity_white_black_in11//};
 = material.TestMaskWithChromeKeyDecal_nodegraph_convert_to_opacity_white_black_in1;
	nodeTmp_convert_to_opacity_white_black_in1 = convert_to_opacity_white_black_in11;// Output connection
	// Graph input TestMaskWithChromeKeyDecal_nodegraph/convert_to_opacity_white_black/in2
	//{
float3 convert_to_opacity_white_black_in21//};
 = material.TestMaskWithChromeKeyDecal_nodegraph_convert_to_opacity_white_black_in2;
	nodeTmp_convert_to_opacity_white_black_in2 = convert_to_opacity_white_black_in21;// Output connection
	// Graph input function call opacity (See definition IM_ifgreater_color3_genglsl)
{
	//{
    float3 convert_to_opacity_white_black_out = (nodeTmp_convert_to_opacity_white_black_value1 > nodeTmp_convert_to_opacity_white_black_value2) ? nodeTmp_convert_to_opacity_white_black_in1 : nodeTmp_convert_to_opacity_white_black_in2;
//};
	nodeOutTmp_convert_to_opacity_white_black_out = convert_to_opacity_white_black_out;// Output connection
	opacity = convert_to_opacity_white_black_out;// Output connection
}
	// Graph input TestMaskWithChromeKeyDecal/thin_walled
	//{
bool TestMaskWithChromeKeyDecal_thin_walled//};
 = material.TestMaskWithChromeKeyDecal_thin_walled;
	thin_walled = TestMaskWithChromeKeyDecal_thin_walled;// Output connection
}
