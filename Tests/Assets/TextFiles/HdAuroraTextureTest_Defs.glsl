
// Definition for implementation IM_texcoord_vector2_genslang
//{
//};

// Definition for implementation IM_image_color3_genslang
//{
    
    void mx_image_color3(sampler2D tex_sampler, int layer, vec3 defaultval, vec2 texcoord, int uaddressmode, int vaddressmode, int filtertype, int framerange, int frameoffset, int frameendaction, vec2 uv_scale, vec2 uv_offset, out vec3 result)
    {
        vec2 uv = mx_transform_uv(texcoord, uv_scale, uv_offset);
        result = texture(tex_sampler, uv).rgb;
    }

//};

// Definition for implementation IM_multiply_vector2FA_genglsl
//{
//};

// Definition for implementation adsk:NG_adsk_bitmap_color3
//{
    void mx_rotate_vector2(vec2 _in, float amount, out vec2 result)
    {
        float rotationRadians = mx_radians(amount);
        float sa = mx_sin(rotationRadians);
        float ca = mx_cos(rotationRadians);
        result = vec2(ca*_in.x + sa*_in.y, -sa*_in.x + ca*_in.y);
    }

    void NG_switch_vector2I(float2 in1, float2 in2, float2 in3, float2 in4, float2 in5, float2 in6, float2 in7, float2 in8, float2 in9, float2 in10, int which, out float2 out1)
    {
        const int ifgreater_10_value1_tmp = 10;
        const float2 ifgreater_10_in2_tmp = float2(0, 0);
        float2 ifgreater_10_out = (ifgreater_10_value1_tmp > which) ? in10 : ifgreater_10_in2_tmp;
        const int ifgreater_9_value1_tmp = 9;
        float2 ifgreater_9_out = (ifgreater_9_value1_tmp > which) ? in9 : ifgreater_10_out;
        const int ifgreater_8_value1_tmp = 8;
        float2 ifgreater_8_out = (ifgreater_8_value1_tmp > which) ? in8 : ifgreater_9_out;
        const int ifgreater_7_value1_tmp = 7;
        float2 ifgreater_7_out = (ifgreater_7_value1_tmp > which) ? in7 : ifgreater_8_out;
        const int ifgreater_6_value1_tmp = 6;
        float2 ifgreater_6_out = (ifgreater_6_value1_tmp > which) ? in6 : ifgreater_7_out;
        const int ifgreater_5_value1_tmp = 5;
        float2 ifgreater_5_out = (ifgreater_5_value1_tmp > which) ? in5 : ifgreater_6_out;
        const int ifgreater_4_value1_tmp = 4;
        float2 ifgreater_4_out = (ifgreater_4_value1_tmp > which) ? in4 : ifgreater_5_out;
        const int ifgreater_3_value1_tmp = 3;
        float2 ifgreater_3_out = (ifgreater_3_value1_tmp > which) ? in3 : ifgreater_4_out;
        const int ifgreater_2_value1_tmp = 2;
        float2 ifgreater_2_out = (ifgreater_2_value1_tmp > which) ? in2 : ifgreater_3_out;
        const int ifgreater_1_value1_tmp = 1;
        float2 ifgreater_1_out = (ifgreater_1_value1_tmp > which) ? in1 : ifgreater_2_out;
        out1 = ifgreater_1_out;
    }

    void NG_place2d_vector2(float2 texcoord, float2 pivot, float2 scale, float rotate, float2 offset, int operationorder, out float2 out1)
    {
        float2 N_subpivot_out = texcoord - pivot;
        float2 N_applyscale_out = N_subpivot_out / scale;
        float2 N_applyoffset2_out = N_subpivot_out - offset;
        float2 N_applyrot_out = float2(0.0);
        mx_rotate_vector2(N_applyscale_out, rotate, N_applyrot_out);
        float2 N_applyrot2_out = float2(0.0);
        mx_rotate_vector2(N_applyoffset2_out, rotate, N_applyrot2_out);
        float2 N_applyoffset_out = N_applyrot_out - offset;
        float2 N_applyscale2_out = N_applyrot2_out / scale;
        float2 N_addpivot_out = N_applyoffset_out + pivot;
        float2 N_addpivot2_out = N_applyscale2_out + pivot;
        float2 N_switch_operationorder_out = float2(0.0);
        NG_switch_vector2I(N_addpivot_out, N_addpivot2_out, float2(0, 0), float2(0, 0), float2(0, 0), float2(0, 0), float2(0, 0), float2(0, 0), float2(0, 0), float2(0, 0), operationorder, N_switch_operationorder_out);
        out1 = N_switch_operationorder_out;
    }

    void adsk_NG_adsk_bitmap_color3(SamplerTexture2D file, float2 realworld_offset, float2 realworld_scale, float2 uv_offset, float2 uv_scale, float rotation_angle, float rgbamount, bool invert, int uaddressmode, int vaddressmode, float2 texcoord, out float3 out1)
    {
        float2 total_offset_out = realworld_offset + uv_offset;
        float2 total_scale_out = realworld_scale / uv_scale;
        const float rotation_angle_param_in2_tmp = -1;
        float rotation_angle_param_out = rotation_angle * rotation_angle_param_in2_tmp;
        float2 a_place2d_out = float2(0.0);
        NG_place2d_vector2(texcoord, float2(0, 0), total_scale_out, rotation_angle_param_out, total_offset_out, 1, a_place2d_out);
        float3 b_image_out = float3(0.0);
        mx_image_color3(file, 0, float3(0, 0, 0), a_place2d_out, uaddressmode, vaddressmode, 1, 0, 0, 0, float2(1, 1), float2(0, 0), b_image_out);
        float3 image_brightness_out = b_image_out * rgbamount;
        const float3 image_invert_amount_tmp = float3(1, 1, 1);
        float3 image_invert_out = image_invert_amount_tmp - image_brightness_out;
        const bool image_convert_value2_tmp = true;
        float3 image_convert_out = (invert == image_convert_value2_tmp) ? image_invert_out : image_brightness_out;
        out1 = image_convert_out;
    }

//};

// Definition for implementation IM_difference_color3_genglsl
//{
//};

// Definition for implementation NG_convert_color3_vector3
//{
    void NG_separate3_color3(float3 in1, out float outr, out float outg, out float outb)
    {
        const int N_extract_0_index_tmp = 0;
        float N_extract_0_out = in1[N_extract_0_index_tmp];
        const int N_extract_1_index_tmp = 1;
        float N_extract_1_out = in1[N_extract_1_index_tmp];
        const int N_extract_2_index_tmp = 2;
        float N_extract_2_out = in1[N_extract_2_index_tmp];
        outr = N_extract_0_out;
        outg = N_extract_1_out;
        outb = N_extract_2_out;
    }

    void NG_convert_color3_vector3(float3 in1, out float3 out1)
    {
        float separate_outr = 0.0;
        float separate_outg = 0.0;
        float separate_outb = 0.0;
        NG_separate3_color3(in1, separate_outr, separate_outg, separate_outb);
        float3 combine_out = vec3(separate_outr,separate_outg,separate_outb);
        out1 = combine_out;
    }

//};

// Definition for implementation IM_magnitude_vector3_genglsl
//{
//};

// Definition for implementation IM_ifgreater_color3_genglsl
//{
//};

// Definition for implementation IM_image_float_genslang
//{
    
    void mx_image_float(sampler2D tex_sampler, int layer, float defaultval, vec2 texcoord, int uaddressmode, int vaddressmode, int filtertype, int framerange, int frameoffset, int frameendaction, vec2 uv_scale, vec2 uv_offset, out float result)
    {
        vec2 uv = mx_transform_uv(texcoord, uv_scale, uv_offset);
        result = texture(tex_sampler, uv).r;
    }

//};
