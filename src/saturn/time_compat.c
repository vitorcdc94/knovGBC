#include <time.h>

#include <yaul.h>

#define SECONDS_PER_DAY 86400LL
#define FALLBACK_EPOCH 946684800LL /* 2000-01-01 00:00:00 UTC */

static bool
is_leap_year(int year)
{
    return ((year % 4) == 0) && (((year % 100) != 0) || ((year % 400) == 0));
}

static int
days_in_month(int year, int month)
{
    static const int days[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };

    if ((month == 1) && is_leap_year(year)) {
        return 29;
    }
    return days[month];
}

static bool
valid_calendar_time(const smpc_time_dec_t *value)
{
    return (value->year >= 1970) && (value->month >= SMPC_TIME_JANUARY) &&
        (value->month <= SMPC_TIME_DECEMBER) && (value->day >= 1) &&
        (value->day <= days_in_month(value->year, value->month - 1)) &&
        (value->hours < 24) && (value->minutes < 60) && (value->seconds < 60);
}

static time_t
calendar_to_epoch(int year, int month, int day, int hour, int minute, int second)
{
    int64_t days = 0;

    for (int current = 1970; current < year; current++) {
        days += is_leap_year(current) ? 366 : 365;
    }
    for (int current = 0; current < month; current++) {
        days += days_in_month(year, current);
    }
    days += day - 1;

    return (time_t)((days * SECONDS_PER_DAY) + (hour * 3600) +
        (minute * 60) + second);
}

time_t
time(time_t *result)
{
    smpc_time_dec_t clock;
    smpc_rtc_time_bcd_from(smpc_rtc_time_get(), &clock);

    const time_t value = valid_calendar_time(&clock)
        ? calendar_to_epoch(clock.year, clock.month - 1, clock.day,
            clock.hours, clock.minutes, clock.seconds)
        : FALLBACK_EPOCH;

    if (result != NULL) {
        *result = value;
    }
    return value;
}

struct tm *
localtime(const time_t *value)
{
    static struct tm result;
    time_t seconds = (value == NULL) ? time(NULL) : *value;
    int64_t days = seconds / SECONDS_PER_DAY;
    int64_t seconds_of_day = seconds % SECONDS_PER_DAY;
    int year = 1970;

    if (seconds_of_day < 0) {
        seconds_of_day += SECONDS_PER_DAY;
        days--;
    }
    while (days >= (is_leap_year(year) ? 366 : 365)) {
        days -= is_leap_year(year) ? 366 : 365;
        year++;
    }
    while (days < 0) {
        year--;
        days += is_leap_year(year) ? 366 : 365;
    }

    int month = 0;
    while (days >= days_in_month(year, month)) {
        days -= days_in_month(year, month);
        month++;
    }

    result.tm_sec = seconds_of_day % 60;
    result.tm_min = (seconds_of_day / 60) % 60;
    result.tm_hour = seconds_of_day / 3600;
    result.tm_mday = (int)days + 1;
    result.tm_mon = month;
    result.tm_year = year - 1900;
    result.tm_yday = 0;
    for (int current = 0; current < month; current++) {
        result.tm_yday += days_in_month(year, current);
    }
    result.tm_yday += (int)days;
    result.tm_wday = (int)((seconds / SECONDS_PER_DAY + 4) % 7);
    if (result.tm_wday < 0) {
        result.tm_wday += 7;
    }
    result.tm_isdst = 0;
    return &result;
}

time_t
mktime(struct tm *value)
{
    if ((value == NULL) || (value->tm_mon < 0) || (value->tm_mon > 11) ||
        (value->tm_mday < 1) ||
        (value->tm_mday > days_in_month(value->tm_year + 1900, value->tm_mon)) ||
        (value->tm_hour < 0) || (value->tm_hour > 23) ||
        (value->tm_min < 0) || (value->tm_min > 59) ||
        (value->tm_sec < 0) || (value->tm_sec > 59)) {
        return (time_t)-1;
    }

    const time_t result = calendar_to_epoch(value->tm_year + 1900,
        value->tm_mon, value->tm_mday, value->tm_hour, value->tm_min,
        value->tm_sec);
    const struct tm *const normalized = localtime(&result);
    *value = *normalized;
    return result;
}
