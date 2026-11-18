Shader "Hidden/HumanVision/RuntimePreviewColor"
{
    Properties { _MainTex ("Camera", 2D) = "white" {} }
    SubShader
    {
        Cull Off ZWrite Off ZTest Always
        Pass
        {
            CGPROGRAM
            #pragma vertex vert_img
            #pragma fragment frag
            #include "UnityCG.cginc"

            sampler2D _MainTex;

            half4 frag(v2f_img input) : SV_Target
            {
                half3 encoded = tex2D(_MainTex, input.uv).rgb;
                half3 rgbLinear = lerp(encoded / 12.92h, pow((encoded + 0.055h) / 1.055h, 2.4h),
                    step(0.04045h, encoded));
                return half4(rgbLinear, 1);
            }
            ENDCG
        }
    }
}
