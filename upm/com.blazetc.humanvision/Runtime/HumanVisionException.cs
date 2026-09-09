using System;

namespace HumanVision
{
    public sealed class HumanVisionException : Exception
    {
        internal HumanVisionException(string operation, int resultCode, string nativeMessage)
            : base(BuildMessage(operation, resultCode, nativeMessage))
        {
            Operation = operation;
            ResultCode = resultCode;
        }

        public string Operation { get; }
        public int ResultCode { get; }

        private static string BuildMessage(string operation, int resultCode, string nativeMessage)
        {
            string detail = string.IsNullOrWhiteSpace(nativeMessage)
                ? "No native error detail was provided."
                : nativeMessage;
            return $"HumanVision {operation} failed ({resultCode}): {detail}";
        }
    }
}
