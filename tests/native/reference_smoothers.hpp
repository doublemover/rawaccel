// Uncached reference equations retained to verify exact cache equivalence.
#pragma once
namespace reference { using namespace rawaccel;
    struct simple_ema_smoother {

        double windowCoefficient = 0;
        double cutoffCoefficient = 0;

        void init(const double halfLife)
        {
            windowTotal = 0;
            cutoffTotal = 0;

            windowCoefficient = halfLife > 0 ? pow(0.5, 1 / halfLife) : 0;
            cutoffCoefficient = 1.0 - sqrt(1.0 - windowCoefficient);
        }

        double windowTotal = 0;
        double cutoffTotal = 0;

        double smooth(const double speed, const milliseconds time)
        {
            // compute coefficients
            double timeAdjustedWindowCoefficient = 1 - pow(windowCoefficient, time);
            double timeAdjustedCutoffCoefficient = 1 - pow(cutoffCoefficient, time);

            // adjust total based on coefficient and difference between new value and total
            windowTotal += timeAdjustedWindowCoefficient * (speed - windowTotal);
            cutoffTotal += timeAdjustedCutoffCoefficient * (speed - cutoffTotal);

            return min(windowTotal, cutoffTotal);
        }
    };

    /// <summary>
    /// Linear exponential moving average smoother.
    /// </summary>
    struct linear_ema_smoother {

        // This constant found via experimentation.
        // Allowing user to specify may confuse parameterization without much gain.
        static constexpr double trendDampening = 0.75;

        double windowCoefficient = 0;
        double cutoffCoefficient = 0;
        double windowTrendCoefficient = 0;
        double cutoffTrendCoefficient = 0;
        bool is_init = false;

        void init(const double halfLife, const double trendHalfLife)
        {
            windowTotal = 0;
            cutoffTotal = 0;
            windowTrendTotal = 0;
            cutoffTrendTotal = 0;

            windowCoefficient = halfLife > 0 ? pow(0.5, 1 / halfLife) : 0;
            windowTrendCoefficient = trendHalfLife > 0 ? pow(0.5, 1 / trendHalfLife) : 0;
            cutoffCoefficient = 1.0 - sqrt(1.0 - windowCoefficient);
            cutoffTrendCoefficient = 1.0 - sqrt(1.0 - windowTrendCoefficient);
        }

        double windowTotal = 0;
        double cutoffTotal = 0;
        double windowTrendTotal = 0;
        double cutoffTrendTotal = 0;

        double smooth(const double speed, const milliseconds time)
        {
            // compute coefficients
            double timeAdjustedWindowCoefficient = 1 - pow(windowCoefficient, time);
            double timeAdjustedCutoffCoefficient = 1 - pow(cutoffCoefficient, time);

            double timeAdjustedWindowTrendCoefficient = 1 - pow(windowTrendCoefficient, time);
            double timeAdjustedCutoffTrendCoefficient = 1 - pow(cutoffTrendCoefficient, time);

            // save old totals for trend adjustment
            double oldWindowTotal = windowTotal;
            double oldCutoffTotal = cutoffTotal;

            // dampen trends
            windowTrendTotal *= trendDampening;
            cutoffTrendTotal *= trendDampening;

            // add dampened trend
            windowTotal += windowTrendTotal * time;
            cutoffTotal += cutoffTrendTotal * time;

            // adjust total based on coefficient and difference between new value and total
            windowTotal += timeAdjustedWindowCoefficient * (speed - windowTotal);
            cutoffTotal += timeAdjustedCutoffCoefficient * (speed - cutoffTotal);

            // don't let trend carry us below 0
            windowTotal = max(windowTotal, 0.0);
            cutoffTotal = max(cutoffTotal, 0.0);

            // adjust trend based on coefficient and difference between new value and total
            double newWindowTrend = time > 0 ? (windowTotal - oldWindowTotal) / time : 0;
            double newCutoffTrend = time > 0 ? (cutoffTotal - oldCutoffTotal) / time : 0;
            windowTrendTotal += timeAdjustedWindowTrendCoefficient * (newWindowTrend - windowTrendTotal);
            cutoffTrendTotal += timeAdjustedCutoffTrendCoefficient * (newCutoffTrend - cutoffTrendTotal);

            return min(windowTotal, cutoffTotal);
        }
    };

}
