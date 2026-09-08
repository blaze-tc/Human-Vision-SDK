Shader "Hidden/HumanVision/CameraOrientation" {
    Properties { _MainTex ("Camera", 2D) = "white" {} }
    SubShader {
        Cull Off ZWrite Off ZTest Always
        Pass {
            CGPROGRAM
            #pragma vertex vert_img
            #pragma fragment frag
            #include "UnityCG.cginc"
            sampler2D _MainTex;
            float _Rotation, _FlipY, _Mirror;
            fixed4 frag(v2f_img i) : SV_Target {
                float2 uv = i.uv;
                if (_Mirror > .5) uv.x = 1 - uv.x;
                if (_Rotation > 2.5) uv = float2(1 - uv.y, uv.x);
                else if (_Rotation > 1.5) uv = 1 - uv;
                else if (_Rotation > .5) uv = float2(uv.y, 1 - uv.x);
                if (_FlipY > .5) uv.y = 1 - uv.y;
                return tex2D(_MainTex, uv);
            }
            ENDCG
        }
    }
}
