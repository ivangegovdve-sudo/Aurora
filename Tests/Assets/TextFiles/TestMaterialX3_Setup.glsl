void setupMaterial_eff66640139564d9(
	Material_eff66640139564d9 material,
	sampler2D base_color_image_image_parameter,
	out float3 base_color)
{
	//Temp input variables for base_color_image 
	float3 nodeOutTmp_base_color_image_out; //Temp output variable for out 
	SamplerTexture2D nodeTmp_base_color_image_file; //Temp input variable for file 
	int nodeTmp_base_color_image_layer; //Temp input variable for layer 
	float3 nodeTmp_base_color_image_default; //Temp input variable for default 
	float2 nodeTmp_base_color_image_texcoord; //Temp input variable for texcoord 
	int nodeTmp_base_color_image_uaddressmode; //Temp input variable for uaddressmode 
	int nodeTmp_base_color_image_vaddressmode; //Temp input variable for vaddressmode 
	int nodeTmp_base_color_image_filtertype; //Temp input variable for filtertype 
	int nodeTmp_base_color_image_framerange; //Temp input variable for framerange 
	int nodeTmp_base_color_image_frameoffset; //Temp input variable for frameoffset 
	int nodeTmp_base_color_image_frameendaction; //Temp input variable for frameendaction 
	float2 nodeTmp_base_color_image_uv_scale; //Temp input variable for uv_scale 
	float2 nodeTmp_base_color_image_uv_offset; //Temp input variable for uv_offset 
	// Graph input NG1/base_color_image/file
	//{
SamplerTexture2D base_color_image_file1//};
 = base_color_image_image_parameter;
	nodeTmp_base_color_image_file = base_color_image_file1;// Output connection
	// Graph input NG1/base_color_image/layer
	//{
int base_color_image_layer1 = 0//};
;
	nodeTmp_base_color_image_layer = base_color_image_layer1;// Output connection
	// Graph input NG1/base_color_image/default
	//{
float3 base_color_image_default1 = float3(0, 0, 0)//};
;
	nodeTmp_base_color_image_default = base_color_image_default1;// Output connection
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
	nodeTmp_base_color_image_texcoord = geomprop_UV0_out1;// Output connection
}
	// Graph input NG1/base_color_image/uaddressmode
	//{
int base_color_image_uaddressmode1 = 2//};
;
	nodeTmp_base_color_image_uaddressmode = base_color_image_uaddressmode1;// Output connection
	// Graph input NG1/base_color_image/vaddressmode
	//{
int base_color_image_vaddressmode1 = 2//};
;
	nodeTmp_base_color_image_vaddressmode = base_color_image_vaddressmode1;// Output connection
	// Graph input NG1/base_color_image/filtertype
	//{
int base_color_image_filtertype1 = 1//};
;
	nodeTmp_base_color_image_filtertype = base_color_image_filtertype1;// Output connection
	// Graph input NG1/base_color_image/framerange
	//{
int base_color_image_framerange1 = 0//};
;
	nodeTmp_base_color_image_framerange = base_color_image_framerange1;// Output connection
	// Graph input NG1/base_color_image/frameoffset
	//{
int base_color_image_frameoffset1 = 0//};
;
	nodeTmp_base_color_image_frameoffset = base_color_image_frameoffset1;// Output connection
	// Graph input NG1/base_color_image/frameendaction
	//{
int base_color_image_frameendaction1 = 0//};
;
	nodeTmp_base_color_image_frameendaction = base_color_image_frameendaction1;// Output connection
	// Graph input 
	//{
float2 base_color_image_uv_scale1 = float2(1, 1)//};
;
	nodeTmp_base_color_image_uv_scale = base_color_image_uv_scale1;// Output connection
	// Graph input 
	//{
float2 base_color_image_uv_offset1 = float2(0, 0)//};
;
	nodeTmp_base_color_image_uv_offset = base_color_image_uv_offset1;// Output connection
	// Graph input function call base_color (See definition IM_image_color3_genslang)
{
	//{
    float3 base_color_image_out = float3(0.0);
    mx_image_color3(nodeTmp_base_color_image_file, nodeTmp_base_color_image_layer, nodeTmp_base_color_image_default, nodeTmp_base_color_image_texcoord, nodeTmp_base_color_image_uaddressmode, nodeTmp_base_color_image_vaddressmode, nodeTmp_base_color_image_filtertype, nodeTmp_base_color_image_framerange, nodeTmp_base_color_image_frameoffset, nodeTmp_base_color_image_frameendaction, nodeTmp_base_color_image_uv_scale, nodeTmp_base_color_image_uv_offset, base_color_image_out);
//};
	nodeOutTmp_base_color_image_out = base_color_image_out;// Output connection
	base_color = base_color_image_out;// Output connection
}
}
