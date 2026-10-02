Shader "Hidden/HumanVision/InputOrientation" {
 Properties { _MainTex ("Texture", 2D) = "white" {} }
 SubShader { Cull Off ZWrite Off ZTest Always
 Pass { CGPROGRAM
 #pragma vertex vert_img
 #pragma fragment frag
 #include "UnityCG.cginc"
 sampler2D _MainTex;float4 _Transform;
 fixed4 frag(v2f_img i):SV_Target {float2 uv=i.uv;if(_Transform.z>.5)uv.x=1-uv.x;
 if(_Transform.x==1)uv=float2(1-uv.y,uv.x);else if(_Transform.x==2)uv=1-uv;else if(_Transform.x==3)uv=float2(uv.y,1-uv.x);
 if(_Transform.y>.5)uv.y=1-uv.y;return tex2D(_MainTex,uv);}
 ENDCG }
 }
}
