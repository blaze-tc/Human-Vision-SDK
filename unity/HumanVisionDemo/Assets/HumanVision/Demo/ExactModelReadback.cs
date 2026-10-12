using System;
using Unity.Collections;

namespace HumanVision.Demo
{
    /// <summary>Device-local qualification. Any mismatch permanently disables this
    /// candidate until the input generation or geometry is replaced.</summary>
    internal sealed class ModelReadbackQualification
    {
        internal int Compared { get; private set; }
        internal bool Rejected { get; private set; }
        internal bool Qualified => !Rejected && Compared >= 3;
        internal void Observe(bool exact)
        {
            if (Rejected) return;
            if (!exact) { Rejected = true; return; }
            ++Compared;
        }
    }
    /// <summary>Frozen RGB resize oracle matching the existing native 11-bit uint8 interpolation.
    /// Used for three same-frame startup comparisons, never on the warmed inference hot path.</summary>
    internal static class ExactModelReadback
    {
        internal const int Width = 512, Height = 288;
        internal static bool Equal(NativeArray<byte> source, int width, int height, NativeArray<byte> candidate,
            out int differences, out int maximumError)
        {
            differences = maximumError = 0;
            if (width < Width || height < Height || width * 9L != height * 16L ||
                source.Length < width * height * 4 || candidate.Length < Width * Height * 4) return false;
            for (int y=0;y<Height;++y)
            {
                float fy=(float)((y+.5)*(double)height/Height-.5);
                int y0=(int)Math.Floor(fy); fy-=y0;
                int by0=(int)Math.Round((1f-fy)*2048f),by1=(int)Math.Round(fy*2048f);
                int row0=Math.Max(0,Math.Min(height-1,y0))*width*4;
                int row1=Math.Max(0,Math.Min(height-1,y0+1))*width*4;
                for (int x=0;x<Width;++x)
                {
                    float fx=(float)((x+.5)*(double)width/Width-.5);
                    int x0=(int)Math.Floor(fx);fx-=x0;
                    if(x0<0){x0=0;fx=0;}if(x0>=width-1){x0=width-1;fx=0;}
                    int ax0=(int)Math.Round((1f-fx)*2048f),ax1=(int)Math.Round(fx*2048f);
                    int left=x0*4,right=Math.Min(width-1,x0+1)*4;
                    for(int c=0;c<3;++c) {
                        int a=source[row0+left+c]*ax0+source[row0+right+c]*ax1;
                        int b=source[row1+left+c]*ax0+source[row1+right+c]*ax1;
                        int expected=(((by0*(a>>4))>>16)+((by1*(b>>4))>>16)+2)>>2;
                        int error=Math.Abs(expected-candidate[(y*Width+x)*4+c]);
                        if(error!=0){++differences;maximumError=Math.Max(maximumError,error);}
                    }
                }
            }
            // Alpha never enters the native RGB tensor. Every RGB byte must agree.
            return differences==0;
        }
    }
}
