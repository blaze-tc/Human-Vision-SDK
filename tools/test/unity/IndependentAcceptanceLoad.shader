Shader "Hidden/HumanVision/IndependentAcceptanceLoad" {
 SubShader { Cull Off ZWrite Off ZTest Always Pass {
  CGPROGRAM
  #pragma vertex vert_img
  #pragma fragment frag
  #include "UnityCG.cginc"
  sampler2D _MainTex;
  fixed4 frag(v2f_img i):SV_Target {
   float2 p=i.uv;
   float value=0.01;
   for(int k=0;k<96;k++) value+=sin(dot(p,float2(12.9898,78.233))+value+float(k))*0.001;
   return fixed4(value,value*0.5,value*0.25,1);
  }
  ENDCG
 } }
}
