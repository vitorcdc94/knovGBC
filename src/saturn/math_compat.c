#include "math_compat.h"

#define TWO_PI (2.0 * M_PI)
#define HALF_PI (M_PI / 2.0)
#define LN2 0.69314718055994530942

int
abs(int value)
{
    return (value < 0) ? -value : value;
}

double
fabs(double value)
{
    return (value < 0.0) ? -value : value;
}

double
floor(double value)
{
    const long long integer = (long long)value;
    return (value < (double)integer) ? (double)(integer - 1) : (double)integer;
}

double
ceil(double value)
{
    const long long integer = (long long)value;
    return (value > (double)integer) ? (double)(integer + 1) : (double)integer;
}

double
round(double value)
{
    return (value >= 0.0) ? floor(value + 0.5) : ceil(value - 0.5);
}

double
sqrt(double value)
{
    if (value <= 0.0) {
        return 0.0;
    }

    double estimate = (value > 1.0) ? value : 1.0;
    for (unsigned iteration = 0; iteration < 18; iteration++) {
        estimate = (estimate + (value / estimate)) * 0.5;
    }
    return estimate;
}

double
sin(double value)
{
    value -= floor(value / TWO_PI) * TWO_PI;
    if (value > M_PI) {
        value -= TWO_PI;
    }
    if (value > HALF_PI) {
        value = M_PI - value;
    } else if (value < -HALF_PI) {
        value = -M_PI - value;
    }

    const double squared = value * value;
    return value * (1.0 + squared * (-1.0 / 6.0 + squared *
        (1.0 / 120.0 + squared * (-1.0 / 5040.0 + squared *
        (1.0 / 362880.0 + squared * (-1.0 / 39916800.0))))));
}

double
cos(double value)
{
    return sin(value + HALF_PI);
}

static double
math_log(double value)
{
    int exponent = 0;
    while (value > 1.5) {
        value *= 0.5;
        exponent++;
    }
    while (value < 0.75) {
        value *= 2.0;
        exponent--;
    }

    const double z = (value - 1.0) / (value + 1.0);
    const double squared = z * z;
    double term = z;
    double sum = term;
    for (unsigned divisor = 3; divisor <= 19; divisor += 2) {
        term *= squared;
        sum += term / (double)divisor;
    }
    return (2.0 * sum) + ((double)exponent * LN2);
}

static double
math_exp(double value)
{
    if (value <= -64.0) {
        return 0.0;
    }
    if (value >= 64.0) {
        return 1.0e27;
    }

    const int exponent = (int)floor(value / LN2);
    const double reduced = value - ((double)exponent * LN2);
    double term = 1.0;
    double sum = 1.0;
    for (unsigned divisor = 1; divisor <= 16; divisor++) {
        term *= reduced / (double)divisor;
        sum += term;
    }

    if (exponent >= 0) {
        for (int index = 0; index < exponent; index++) {
            sum *= 2.0;
        }
    } else {
        for (int index = 0; index > exponent; index--) {
            sum *= 0.5;
        }
    }
    return sum;
}

double
pow(double base, double exponent)
{
    if (base == 0.0) {
        return (exponent > 0.0) ? 0.0 : 1.0;
    }
    if (base < 0.0) {
        const double integral = round(exponent);
        if (integral != exponent) {
            return 0.0;
        }

        double result = math_exp(exponent * math_log(-base));
        return (((long long)integral & 1LL) != 0) ? -result : result;
    }
    return math_exp(exponent * math_log(base));
}
