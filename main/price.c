// Electricity prices from a Tibber API response, and the short-term trend.
//
// tibber.c fetches the response; the current quarter is picked with the
// (SNTP) clock, so one fetch keeps working until tomorrow's prices run out.
// fixtures/priceInfo.json shows the response format.

#include "price.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"

#define TAG "price"

// Trend bands, relative to the current price: within +-2 % = equal,
// beyond +-10 % = hard up/down.
#define PRICE_TREND_BAND      0.02
#define PRICE_TREND_HARD_BAND 0.10

#define PRICE_QUARTER_S (15 * 60)

static const cJSON *get_path(const cJSON *node, const char *const *keys, int n)
{
    for (int i = 0; i < n && node != NULL; i++) {
        node = cJSON_GetObjectItemCaseSensitive(node, keys[i]);
    }
    return node;
}

// Days since 1970-01-01 for a proleptic Gregorian date (H. Hinnant's
// days_from_civil): no timegm()/TZ needed.
static int64_t days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const int yoe = y - era * 400;
    const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + doe - 719468;
}

// "2026-10-29T13:30:00.000+01:00" -> UTC time_t.
static bool parse_iso8601(const char *s, time_t *out)
{
    int y, mo, d, h, mi, sec;
    if (sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &h, &mi, &sec) != 6) {
        return false;
    }
    // Offset after the optional fraction: "+01:00", "-05:00" or "Z".
    int offset_s = 0;
    const char *tz = strpbrk(s + 19, "+-Z");
    if (tz != NULL && *tz != 'Z') {
        int oh, om;
        if (sscanf(tz + 1, "%2d:%2d", &oh, &om) != 2) {
            return false;
        }
        offset_s = (oh * 3600 + om * 60) * (*tz == '-' ? -1 : 1);
    }
    *out = (time_t)(days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + sec - offset_s);
    return true;
}

static void add_quarters(const cJSON *quarters, price_table_t *out)
{
    const cJSON *q = NULL;
    cJSON_ArrayForEach(q, quarters) {
        if (out->count >= PRICE_MAX_QUARTERS) {
            return;
        }
        const cJSON *total = cJSON_GetObjectItemCaseSensitive(q, "total");
        const cJSON *start = cJSON_GetObjectItemCaseSensitive(q, "startsAt");
        time_t t;
        if (cJSON_IsNumber(total) && cJSON_IsString(start) && parse_iso8601(start->valuestring, &t)) {
            out->q[out->count].start = t;
            out->q[out->count].total = total->valuedouble;
            out->count++;
        }
    }
}

bool price_parse_tibber(const char *json, price_table_t *out)
{
    out->count = 0;

    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        ESP_LOGW(TAG, "Invalid JSON");
        return false;
    }

    // A home without an active subscription has priceInfo null; skip it.
    const char *const to_homes[] = {"data", "viewer", "homes"};
    const char *const to_price_info[] = {"currentSubscription", "priceInfo"};
    const cJSON *homes = get_path(root, to_homes, 3);
    const cJSON *home = NULL;
    const cJSON *price_info = NULL;
    cJSON_ArrayForEach(home, homes) {
        price_info = get_path(home, to_price_info, 2);
        if (cJSON_IsObject(price_info)) {
            break;
        }
        price_info = NULL;
    }

    if (price_info == NULL) {
        ESP_LOGW(TAG, "No home with priceInfo in response");
    } else {
        add_quarters(cJSON_GetObjectItemCaseSensitive(price_info, "today"), out);
        add_quarters(cJSON_GetObjectItemCaseSensitive(price_info, "tomorrow"), out);
        if (out->count == 0) {
            ESP_LOGW(TAG, "No prices in priceInfo.today/tomorrow");
        }
    }

    cJSON_Delete(root);
    return out->count > 0;
}

bool price_at(const price_table_t *t, time_t now, price_info_t *out)
{
    memset(out, 0, sizeof(*out));
    for (int i = 0; i < t->count; i++) {
        time_t end = i + 1 < t->count ? t->q[i + 1].start : t->q[i].start + PRICE_QUARTER_S;
        if (now < t->q[i].start || now >= end) {
            continue;
        }
        out->current = t->q[i].total;
        for (int j = i + 1; j < t->count && out->next_count < PRICE_LOOKAHEAD; j++) {
            out->next[out->next_count++] = t->q[j].total;
        }
        return true;
    }
    return false;
}

double price_next_average(const price_info_t *p)
{
    if (p->next_count == 0) {
        return p->current;
    }
    double sum = 0;
    for (int i = 0; i < p->next_count; i++) {
        sum += p->next[i];
    }
    return sum / p->next_count;
}

price_trend_t price_trend(const price_info_t *p)
{
    double base = fabs(p->current);  // fabs: negative prices exist
    double diff = price_next_average(p) - p->current;
    if (diff > base * PRICE_TREND_HARD_BAND) {
        return PRICE_TREND_UP_HARD;
    }
    if (diff > base * PRICE_TREND_BAND) {
        return PRICE_TREND_UP;
    }
    if (diff < -base * PRICE_TREND_HARD_BAND) {
        return PRICE_TREND_DOWN_HARD;
    }
    if (diff < -base * PRICE_TREND_BAND) {
        return PRICE_TREND_DOWN;
    }
    return PRICE_TREND_EQUAL;
}
