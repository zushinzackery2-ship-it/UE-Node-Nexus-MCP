// Math type stubs for compiler-only checks; see CoreTypes.h for the rationale.
#pragma once

struct FVector
{
    double X = 0.0;
    double Y = 0.0;
    double Z = 0.0;

    FVector() = default;
    FVector(double InX, double InY, double InZ);

    bool IsNearlyZero(double Tolerance = 1.e-4) const;
    struct FRotator Rotation() const;
    FVector operator-(const FVector& Other) const;

    static const FVector ZeroVector;
    static const FVector OneVector;
};

struct FRotator
{
    double Pitch = 0.0;
    double Yaw = 0.0;
    double Roll = 0.0;

    FRotator() = default;
    FRotator(double InPitch, double InYaw, double InRoll);

    static const FRotator ZeroRotator;
};

struct FTransform
{
    FTransform() = default;
};
