namespace HumanVision
{
    /// <summary>语义计算模式。Graphics 使用图形处理器；Neural 请求专用神经网络加速器；Cpu 使用已安装的 CPU 模型。</summary>
    public enum HumanVisionAccelerationMode { Graphics = 0, Neural = 1, Cpu = 2 }
}
