namespace HumanVision
{
    public enum HumanVisionCanonicalJointId
    {
        Pelvis, SpineNavel, SpineChest, Neck, ClavicleLeft, ShoulderLeft, ElbowLeft,
        WristLeft, HandLeft, HandtipLeft, ThumbLeft, ClavicleRight, ShoulderRight,
        ElbowRight, WristRight, HandRight, HandtipRight, ThumbRight, HipLeft, KneeLeft,
        AnkleLeft, FootLeft, HipRight, KneeRight, AnkleRight, FootRight, Head, Nose,
        EyeLeft, EarLeft, EyeRight, EarRight
    }
    public readonly struct HumanVisionCanonicalJoint
    {
        public HumanVisionJoint Position { get; }
        public long ObservationTimestampUs { get; }
        public float PredictionMilliseconds { get; }
        internal HumanVisionCanonicalJoint(HumanVisionJoint position, long time, float prediction)
        { Position = position; ObservationTimestampUs = time; PredictionMilliseconds = prediction; }
    }
}
