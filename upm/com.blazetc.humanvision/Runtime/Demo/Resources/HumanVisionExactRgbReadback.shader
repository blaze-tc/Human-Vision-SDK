Shader "Hidden/HumanVision/ExactRgbReadback"
{
    // Graphics.Blit binds its source and dimensions through this texture property.
    Properties { _MainTex ("Source", 2D) = "white" {} }
    SubShader
    {
        Cull Off ZWrite Off ZTest Always
        Pass
        {
            HLSLPROGRAM
            #pragma vertex vert_img
            #pragma fragment frag
            #pragma target 4.5
            #include "UnityCG.cginc"
            Texture2D<float4> _MainTex;
            float4 _MainTex_TexelSize;
            int3 ReadBytes(int2 coordinate)
            {
                float3 value = _MainTex.Load(int3(coordinate, 0)).rgb;
                #ifndef UNITY_COLORSPACE_GAMMA
                    value = float3(LinearToGammaSpaceExact(value.r),LinearToGammaSpaceExact(value.g),LinearToGammaSpaceExact(value.b));
                #endif
                return (int3)round(saturate(value) * 255.0);
            }
            float4 frag(v2f_img input) : SV_Target
            {
                // SV_Position and Load use the same physical texture row origin.
                // Row normalization remains the existing measured readback policy.
                int2 pixel=(int2)input.pos.xy;
                int2 size=(int2)_MainTex_TexelSize.zw;
                float2 position=(float2(pixel)+0.5)*float2(size)/float2(512,288)-0.5;
                int2 first=(int2)floor(position);
                float2 fraction=position-float2(first);
                if(first.x<0){first.x=0;fraction.x=0;}
                if(first.x>=size.x-1){first.x=size.x-1;fraction.x=0;}
                int2 w0=(int2)round((1.0-fraction)*2048.0);
                int2 w1=(int2)round(fraction*2048.0);
                int x0=clamp(first.x,0,size.x-1),x1=clamp(first.x+1,0,size.x-1);
                int y0=clamp(first.y,0,size.y-1),y1=clamp(first.y+1,0,size.y-1);
                int3 a=ReadBytes(int2(x0,y0))*w0.x+ReadBytes(int2(x1,y0))*w1.x;
                int3 b=ReadBytes(int2(x0,y1))*w0.x+ReadBytes(int2(x1,y1))*w1.x;
                // Preserve the original staged truncation, not ordinary bilinear rounding.
                int3 value=(((w0.y*(a>>4))>>16)+((w1.y*(b>>4))>>16)+2)>>2;
                // Target is explicitly linear so these bytes are not re-encoded as sRGB.
                return float4(float3(value)/255.0,1.0);
            }
            ENDHLSL
        }
    }
}
