#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <stdint.h>

#define TIMEZONE_OFFSET (8 * 3600)
#define EPOCH_OFFSET (1000000000)

typedef struct {
    uint64_t sec;
    uint64_t min;
    uint32_t hour;
    uint32_t day;
    uint16_t month;
    uint8_t wday;
} CronPattern;

/* ============ Cron 实现部分 ============ */

// 设置位的辅助函数
static void set_bit_in_array(void* array, int index, int element_size) {
    if (element_size == 1) {
        uint8_t *arr = (uint8_t*)array;
        *arr |= (1 << index);
    } else if (element_size == 2) {
        uint16_t *arr = (uint16_t*)array;
        *arr |= (1 << index);
    } else if (element_size == 4) {
        uint32_t *arr = (uint32_t*)array;
        *arr |= (1 << index);
    } else if (element_size == 8) {
        uint64_t *arr = (uint64_t*)array;
        *arr |= (1ULL << index);
    }
}

// 获取位的辅助函数
static int get_bit_from_array(const void* array, int index, int element_size) {
    if (element_size == 1) {
        const uint8_t *arr = (const uint8_t*)array;
        return (*arr >> index) & 1;
    } else if (element_size == 2) {
        const uint16_t *arr = (const uint16_t*)array;
        return (*arr >> index) & 1;
    } else if (element_size == 4) {
        const uint32_t *arr = (const uint32_t*)array;
        return (*arr >> index) & 1;
    } else if (element_size == 8) {
        const uint64_t *arr = (const uint64_t*)array;
        return (*arr >> index) & 1;
    }
    return 0;
}

// 解析数字范围，如 "1-5" 或 "*/2"
int parse_range(const char *field, void* array, int min_val, int max_val, int element_size) {
    char *token, *saveptr;
    char field_copy[32];
    int count = 0;
    
    memset(array, 0, element_size);
    
    strcpy(field_copy, field);
    
    token = strtok_r(field_copy, ",", &saveptr);
    while (token != NULL) {
        // 处理 "*" 或 "*/n"
        if (token[0] == '*') {
            int step = 1;
            if (strlen(token) > 1 && token[1] == '/') {
                step = atoi(token + 2);
                if (step <= 0) step = 1;
            }
            for (int i = min_val; i <= max_val; i += step) {
                set_bit_in_array(array, i, element_size);
                count++;
            }
        }
        // 处理范围 "a-b" 或 "a-b/c"
        else if (strchr(token, '-') != NULL) {
            char range_copy[32];
            strcpy(range_copy, token);
            char *dash = strchr(range_copy, '-');
            *dash = '\0';
            int start = atoi(range_copy);
            
            char *end_part = dash + 1;
            int end, step = 1;
            
            if (strchr(end_part, '/') != NULL) {
                char *slash = strchr(end_part, '/');
                *slash = '\0';
                end = atoi(end_part);
                step = atoi(slash + 1);
                if (step <= 0) step = 1;
            } else {
                end = atoi(end_part);
            }
            
            // 确保范围有效
            if (start < min_val) start = min_val;
            if (end > max_val) end = max_val;
            
            for (int i = start; i <= end; i += step) {
                if (i >= min_val && i <= max_val) {
                    set_bit_in_array(array, i, element_size);
                    count++;
                }
            }
        }
        // 处理单个数字
        else {
            int num = atoi(token);
            if (num >= min_val && num <= max_val) {
                set_bit_in_array(array, num, element_size);
                count++;
            }
        }
        
        token = strtok_r(NULL, ",", &saveptr);
    }
    
    return count > 0 ? 0 : -1;
}

// 解析cron表达式
int parse_cron(const char *cron_expr, CronPattern *pattern) {
    memset(pattern, 0, sizeof(CronPattern));
    char expr_copy[128];
    strcpy(expr_copy, cron_expr);
    
    char *fields[6];
    int field_count = 0;
    
    // 分割字段
    char *token = strtok(expr_copy, " \t");
    while (token != NULL && field_count < 6) {
        fields[field_count++] = token;
        token = strtok(NULL, " \t");
    }
    
    int result = 0;
    
    // 支持5字段和6字段两种格式
    if (field_count == 5) {
        // 5字段格式 (分 时 日 月 星期)
        set_bit_in_array(&pattern->sec, 0, 8); // 默认0秒执行
        result |= parse_range(fields[0], &pattern->min, 0, 59, 8);     // 分钟
        result |= parse_range(fields[1], &pattern->hour, 0, 23, 4);    // 小时
        result |= parse_range(fields[2], &pattern->day, 1, 31, 4);     // 日
        result |= parse_range(fields[3], &pattern->month, 1, 12, 2);   // 月
        result |= parse_range(fields[4], &pattern->wday, 0, 7, 1);     // 星期
    } else if (field_count == 6) {
        // 6字段格式 (秒 分 时 日 月 星期)
        result |= parse_range(fields[0], &pattern->sec, 0, 59, 8);     // 秒
        result |= parse_range(fields[1], &pattern->min, 0, 59, 8);     // 分钟
        result |= parse_range(fields[2], &pattern->hour, 0, 23, 4);    // 小时
        result |= parse_range(fields[3], &pattern->day, 1, 31, 4);     // 日
        result |= parse_range(fields[4], &pattern->month, 1, 12, 2);   // 月
        result |= parse_range(fields[5], &pattern->wday, 0, 7, 1);     // 星期
    } else {
        return -1;
    }
    
    // 处理星期0和7都表示周日
    if (get_bit_from_array(&pattern->wday, 0, 1) || get_bit_from_array(&pattern->wday, 7, 1)) {
        set_bit_in_array(&pattern->wday, 0, 1);
        set_bit_in_array(&pattern->wday, 7, 1);
    }
    
    return result;
}

// 检查时间是否匹配cron模式
int match_cron(time_t timestamp, const CronPattern *pattern) {
    // 转换为东八区时间
    timestamp += TIMEZONE_OFFSET;
    struct tm *tm_info = gmtime(&timestamp);
    
    // 检查各字段是否匹配
    if (!get_bit_from_array(&pattern->sec, timestamp % 60, 8)) return 0;        // 秒
    if (!get_bit_from_array(&pattern->min, tm_info->tm_min, 8)) return 0;       // 分
    if (!get_bit_from_array(&pattern->hour, tm_info->tm_hour, 4)) return 0;     // 时
    if (!get_bit_from_array(&pattern->day, tm_info->tm_mday, 4)) return 0;      // 日
    if (!get_bit_from_array(&pattern->month, tm_info->tm_mon + 1, 2)) return 0; // 月
    if (!get_bit_from_array(&pattern->wday, tm_info->tm_wday, 1)) return 0;     // 星期
    
    return 1;
}

/* ============ 测试部分 ============ */

/* 测试计数器 */
static int test_passed = 0;
static int test_failed = 0;

/* 辅助函数 */
void print_test_result(const char* test_name, int passed) {
    if (passed) {
        printf("✓ %s - PASSED\n", test_name);
        test_passed++;
    } else {
        printf("✗ %s - FAILED\n", test_name);
        test_failed++;
    }
}

/* 创建指定时间的 timestamp (东八区) */
time_t create_timestamp(int year, int month, int day, int hour, int min, int sec) {
    struct tm tm_info = {0};
    tm_info.tm_year = year - 1900;
    tm_info.tm_mon = month - 1;
    tm_info.tm_mday = day;
    tm_info.tm_hour = hour;
    tm_info.tm_min = min;
    tm_info.tm_sec = sec;
    
    // 我们想要创建一个 UTC 时间戳，当 match_cron 加上 TIMEZONE_OFFSET 后
    // 能得到东八区的指定时间
    // 所以 UTC 时间 = 东八区时间 - 8小时
    // 使用 timegm 来创建 UTC 时间戳（假设输入已经是 UTC-8 小时）
    #ifdef __APPLE__
        time_t t = timegm(&tm_info) - TIMEZONE_OFFSET;
    #else
        // Linux 使用 timegm
        time_t t = timegm(&tm_info) - TIMEZONE_OFFSET;
    #endif
    
    return t;
}

/* ============ 测试 parse_range 函数 ============ */

void test_parse_range_single_value() {
    uint64_t array[2] = {0};
    int result = parse_range("5", array, 0, 59, 8);
    
    int passed = (result == 0 && get_bit_from_array(array, 5, 8) == 1);
    print_test_result("parse_range: 单个值 '5'", passed);
}

void test_parse_range_multiple_values() {
    uint64_t array[2] = {0};
    int result = parse_range("1,5,10", array, 0, 59, 8);
    
    int passed = (result == 0 && 
                  get_bit_from_array(array, 1, 8) == 1 &&
                  get_bit_from_array(array, 5, 8) == 1 &&
                  get_bit_from_array(array, 10, 8) == 1);
    print_test_result("parse_range: 多个值 '1,5,10'", passed);
}

void test_parse_range_wildcard() {
    uint32_t array[1] = {0};
    int result = parse_range("*", array, 0, 23, 4);
    
    int passed = (result == 0);
    for (int i = 0; i <= 23 && passed; i++) {
        if (get_bit_from_array(array, i, 4) != 1) {
            passed = 0;
        }
    }
    print_test_result("parse_range: 通配符 '*'", passed);
}

void test_parse_range_step() {
    uint64_t array[2] = {0};
    int result = parse_range("*/5", array, 0, 59, 8);
    
    int passed = (result == 0);
    for (int i = 0; i <= 59 && passed; i++) {
        int expected = (i % 5 == 0) ? 1 : 0;
        if (get_bit_from_array(array, i, 8) != expected) {
            passed = 0;
        }
    }
    print_test_result("parse_range: 步长 '*/5'", passed);
}

void test_parse_range_range() {
    uint64_t array[2] = {0};
    int result = parse_range("10-15", array, 0, 59, 8);
    
    int passed = (result == 0);
    for (int i = 10; i <= 15 && passed; i++) {
        if (get_bit_from_array(array, i, 8) != 1) {
            passed = 0;
        }
    }
    // 确保范围外的位为0
    if (passed) {
        passed = (get_bit_from_array(array, 9, 8) == 0 &&
                  get_bit_from_array(array, 16, 8) == 0);
    }
    print_test_result("parse_range: 范围 '10-15'", passed);
}

void test_parse_range_range_with_step() {
    uint64_t array[2] = {0};
    int result = parse_range("0-20/5", array, 0, 59, 8);
    
    int passed = (result == 0 &&
                  get_bit_from_array(array, 0, 8) == 1 &&
                  get_bit_from_array(array, 5, 8) == 1 &&
                  get_bit_from_array(array, 10, 8) == 1 &&
                  get_bit_from_array(array, 15, 8) == 1 &&
                  get_bit_from_array(array, 20, 8) == 1 &&
                  get_bit_from_array(array, 25, 8) == 0);
    print_test_result("parse_range: 范围步长 '0-20/5'", passed);
}

void test_parse_range_complex() {
    uint64_t array[2] = {0};
    int result = parse_range("1,5,10-15,*/20", array, 0, 59, 8);
    
    int passed = (result == 0 &&
                  get_bit_from_array(array, 1, 8) == 1 &&
                  get_bit_from_array(array, 5, 8) == 1 &&
                  get_bit_from_array(array, 10, 8) == 1 &&
                  get_bit_from_array(array, 12, 8) == 1 &&
                  get_bit_from_array(array, 15, 8) == 1 &&
                  get_bit_from_array(array, 20, 8) == 1 &&
                  get_bit_from_array(array, 40, 8) == 1);
    print_test_result("parse_range: 复合表达式 '1,5,10-15,*/20'", passed);
}

/* ============ 测试 parse_cron 函数 ============ */

void test_parse_cron_5_fields() {
    CronPattern pattern;
    // 每天早上8点整执行
    int result = parse_cron("0 8 * * *", &pattern);
    
    int passed = (result == 0 &&
                  get_bit_from_array(&pattern.sec, 0, 8) == 1 &&  // 默认0秒
                  get_bit_from_array(&pattern.min, 0, 8) == 1 &&
                  get_bit_from_array(&pattern.hour, 8, 4) == 1);
    print_test_result("parse_cron: 5字段格式 '0 8 * * *'", passed);
}

void test_parse_cron_6_fields() {
    CronPattern pattern;
    // 每30秒执行一次
    int result = parse_cron("*/30 * * * * *", &pattern);
    
    int passed = (result == 0 &&
                  get_bit_from_array(&pattern.sec, 0, 8) == 1 &&
                  get_bit_from_array(&pattern.sec, 30, 8) == 1);
    print_test_result("parse_cron: 6字段格式 '*/30 * * * * *'", passed);
}

void test_parse_cron_specific_time() {
    CronPattern pattern;
    // 每天下午3点30分执行
    int result = parse_cron("30 15 * * *", &pattern);
    
    int passed = (result == 0 &&
                  get_bit_from_array(&pattern.min, 30, 8) == 1 &&
                  get_bit_from_array(&pattern.hour, 15, 4) == 1);
    print_test_result("parse_cron: 指定时间 '30 15 * * *'", passed);
}

void test_parse_cron_weekday() {
    CronPattern pattern;
    // 每周一到周五早上9点执行
    int result = parse_cron("0 9 * * 1-5", &pattern);
    
    int passed = (result == 0 &&
                  get_bit_from_array(&pattern.wday, 1, 1) == 1 &&
                  get_bit_from_array(&pattern.wday, 5, 1) == 1);
    print_test_result("parse_cron: 工作日 '0 9 * * 1-5'", passed);
}

void test_parse_cron_sunday() {
    CronPattern pattern;
    // 周日执行 (0和7都表示周日)
    int result = parse_cron("0 0 * * 0", &pattern);
    
    int passed = (result == 0 &&
                  get_bit_from_array(&pattern.wday, 0, 1) == 1 &&
                  get_bit_from_array(&pattern.wday, 7, 1) == 1);  // 0和7应该都被设置
    print_test_result("parse_cron: 周日 '0 0 * * 0'", passed);
}

void test_parse_cron_monthly() {
    CronPattern pattern;
    // 每月1日和15日执行
    int result = parse_cron("0 0 1,15 * *", &pattern);
    
    int passed = (result == 0 &&
                  get_bit_from_array(&pattern.day, 1, 4) == 1 &&
                  get_bit_from_array(&pattern.day, 15, 4) == 1);
    print_test_result("parse_cron: 每月特定日期 '0 0 1,15 * *'", passed);
}

void test_parse_cron_every_minute() {
    CronPattern pattern;
    // 每分钟执行
    int result = parse_cron("* * * * *", &pattern);
    
    int passed = (result == 0);
    // 检查所有分钟都被设置
    for (int i = 0; i <= 59 && passed; i++) {
        if (get_bit_from_array(&pattern.min, i, 8) != 1) {
            passed = 0;
        }
    }
    print_test_result("parse_cron: 每分钟 '* * * * *'", passed);
}

void test_parse_cron_invalid() {
    CronPattern pattern;
    // 无效的字段数量
    int result = parse_cron("* * *", &pattern);
    
    int passed = (result == -1);
    print_test_result("parse_cron: 无效格式 '* * *'", passed);
}

/* ============ 测试 match_cron 函数 ============ */

void test_match_cron_exact_time() {
    CronPattern pattern;
    parse_cron("30 15 7 11 *", &pattern);  // 11月7日下午3:30
    
    // 2025-11-07 15:30:00
    time_t t = create_timestamp(2025, 11, 7, 15, 30, 0);
    int matched = match_cron(t, &pattern);
    
    print_test_result("match_cron: 精确匹配时间", matched == 1);
}

void test_match_cron_not_match() {
    CronPattern pattern;
    parse_cron("30 15 * * *", &pattern);  // 每天下午3:30
    
    // 2025-11-07 15:31:00 (分钟不匹配)
    time_t t = create_timestamp(2025, 11, 7, 15, 31, 0);
    int matched = match_cron(t, &pattern);
    
    print_test_result("match_cron: 时间不匹配", matched == 0);
}

void test_match_cron_every_hour() {
    CronPattern pattern;
    parse_cron("0 * * * *", &pattern);  // 每小时整点
    
    time_t t1 = create_timestamp(2025, 11, 7, 10, 0, 0);
    time_t t2 = create_timestamp(2025, 11, 7, 15, 0, 0);
    time_t t3 = create_timestamp(2025, 11, 7, 23, 0, 0);
    
    int passed = (match_cron(t1, &pattern) == 1 &&
                  match_cron(t2, &pattern) == 1 &&
                  match_cron(t3, &pattern) == 1);
    print_test_result("match_cron: 每小时整点", passed);
}

void test_match_cron_weekday() {
    CronPattern pattern;
    parse_cron("0 9 * * 1-5", &pattern);  // 工作日早上9点
    
    // 2025-11-07 是周五
    time_t t_friday = create_timestamp(2025, 11, 7, 9, 0, 0);
    // 2025-11-08 是周六
    time_t t_saturday = create_timestamp(2025, 11, 8, 9, 0, 0);
    // 2025-11-09 是周日
    time_t t_sunday = create_timestamp(2025, 11, 9, 9, 0, 0);
    // 2025-11-10 是周一
    time_t t_monday = create_timestamp(2025, 11, 10, 9, 0, 0);
    
    int passed = (match_cron(t_friday, &pattern) == 1 &&
                  match_cron(t_saturday, &pattern) == 0 &&
                  match_cron(t_sunday, &pattern) == 0 &&
                  match_cron(t_monday, &pattern) == 1);
    print_test_result("match_cron: 工作日匹配", passed);
}

void test_match_cron_with_seconds() {
    CronPattern pattern;
    parse_cron("30 15 10 * * *", &pattern);  // 每天10:15:30
    
    time_t t_match = create_timestamp(2025, 11, 7, 10, 15, 30);
    time_t t_no_match = create_timestamp(2025, 11, 7, 10, 15, 31);
    
    int passed = (match_cron(t_match, &pattern) == 1 &&
                  match_cron(t_no_match, &pattern) == 0);
    print_test_result("match_cron: 带秒的匹配", passed);
}

void test_match_cron_step_values() {
    CronPattern pattern;
    parse_cron("*/15 * * * *", &pattern);  // 每15分钟
    
    time_t t0 = create_timestamp(2025, 11, 7, 10, 0, 0);
    time_t t15 = create_timestamp(2025, 11, 7, 10, 15, 0);
    time_t t30 = create_timestamp(2025, 11, 7, 10, 30, 0);
    time_t t45 = create_timestamp(2025, 11, 7, 10, 45, 0);
    time_t t10 = create_timestamp(2025, 11, 7, 10, 10, 0);
    
    int passed = (match_cron(t0, &pattern) == 1 &&
                  match_cron(t15, &pattern) == 1 &&
                  match_cron(t30, &pattern) == 1 &&
                  match_cron(t45, &pattern) == 1 &&
                  match_cron(t10, &pattern) == 0);
    print_test_result("match_cron: 步长值匹配", passed);
}

void test_match_cron_specific_month() {
    CronPattern pattern;
    parse_cron("0 0 1 1,6,12 *", &pattern);  // 1月、6月、12月的1号
    
    time_t t_jan = create_timestamp(2025, 1, 1, 0, 0, 0);
    time_t t_jun = create_timestamp(2025, 6, 1, 0, 0, 0);
    time_t t_dec = create_timestamp(2025, 12, 1, 0, 0, 0);
    time_t t_nov = create_timestamp(2025, 11, 1, 0, 0, 0);
    
    int passed = (match_cron(t_jan, &pattern) == 1 &&
                  match_cron(t_jun, &pattern) == 1 &&
                  match_cron(t_dec, &pattern) == 1 &&
                  match_cron(t_nov, &pattern) == 0);
    print_test_result("match_cron: 特定月份匹配", passed);
}

void test_match_cron_last_day_of_month() {
    CronPattern pattern;
    parse_cron("0 0 31 * *", &pattern);  // 每月31号
    
    // 1月有31天
    time_t t_jan = create_timestamp(2025, 1, 31, 0, 0, 0);
    // 2月没有31天，这个不应该匹配
    time_t t_feb = create_timestamp(2025, 2, 28, 0, 0, 0);
    
    int passed = (match_cron(t_jan, &pattern) == 1 &&
                  match_cron(t_feb, &pattern) == 0);
    print_test_result("match_cron: 月末日期", passed);
}

/* ============ 边界条件测试 ============ */

void test_boundary_seconds() {
    CronPattern pattern;
    parse_cron("0,59 * * * * *", &pattern);
    
    time_t t0 = create_timestamp(2025, 11, 7, 10, 15, 0);
    time_t t59 = create_timestamp(2025, 11, 7, 10, 15, 59);
    time_t t30 = create_timestamp(2025, 11, 7, 10, 15, 30);
    
    int passed = (match_cron(t0, &pattern) == 1 &&
                  match_cron(t59, &pattern) == 1 &&
                  match_cron(t30, &pattern) == 0);
    print_test_result("边界测试: 秒边界 (0,59)", passed);
}

void test_boundary_hours() {
    CronPattern pattern;
    parse_cron("0 0,23 * * *", &pattern);
    
    time_t t0 = create_timestamp(2025, 11, 7, 0, 0, 0);
    time_t t23 = create_timestamp(2025, 11, 7, 23, 0, 0);
    time_t t12 = create_timestamp(2025, 11, 7, 12, 0, 0);
    
    int passed = (match_cron(t0, &pattern) == 1 &&
                  match_cron(t23, &pattern) == 1 &&
                  match_cron(t12, &pattern) == 0);
    print_test_result("边界测试: 小时边界 (0,23)", passed);
}

void test_boundary_months() {
    CronPattern pattern;
    parse_cron("0 0 1 1,12 *", &pattern);
    
    time_t t1 = create_timestamp(2025, 1, 1, 0, 0, 0);
    time_t t12 = create_timestamp(2025, 12, 1, 0, 0, 0);
    time_t t6 = create_timestamp(2025, 6, 1, 0, 0, 0);
    
    int passed = (match_cron(t1, &pattern) == 1 &&
                  match_cron(t12, &pattern) == 1 &&
                  match_cron(t6, &pattern) == 0);
    print_test_result("边界测试: 月份边界 (1,12)", passed);
}

/* ============ 实际场景测试 ============ */

void test_scenario_backup_daily() {
    CronPattern pattern;
    parse_cron("0 2 * * *", &pattern);  // 每天凌晨2点备份
    
    time_t t_backup = create_timestamp(2025, 11, 7, 2, 0, 0);
    time_t t_normal = create_timestamp(2025, 11, 7, 14, 30, 0);
    
    int passed = (match_cron(t_backup, &pattern) == 1 &&
                  match_cron(t_normal, &pattern) == 0);
    print_test_result("场景测试: 每日备份", passed);
}

void test_scenario_report_weekly() {
    CronPattern pattern;
    parse_cron("0 9 * * 1", &pattern);  // 每周一早上9点生成报告
    
    time_t t_monday = create_timestamp(2025, 11, 10, 9, 0, 0);  // 周一
    time_t t_tuesday = create_timestamp(2025, 11, 11, 9, 0, 0);  // 周二
    
    int passed = (match_cron(t_monday, &pattern) == 1 &&
                  match_cron(t_tuesday, &pattern) == 0);
    print_test_result("场景测试: 周报生成", passed);
}

void test_scenario_cleanup_monthly() {
    CronPattern pattern;
    parse_cron("0 3 1 * *", &pattern);  // 每月1号凌晨3点清理
    
    time_t t_first = create_timestamp(2025, 11, 1, 3, 0, 0);
    time_t t_mid = create_timestamp(2025, 11, 15, 3, 0, 0);
    
    int passed = (match_cron(t_first, &pattern) == 1 &&
                  match_cron(t_mid, &pattern) == 0);
    print_test_result("场景测试: 月度清理", passed);
}

void test_scenario_health_check() {
    CronPattern pattern;
    parse_cron("*/5 * * * *", &pattern);  // 每5分钟健康检查
    
    time_t t5 = create_timestamp(2025, 11, 7, 10, 5, 0);
    time_t t10 = create_timestamp(2025, 11, 7, 10, 10, 0);
    time_t t3 = create_timestamp(2025, 11, 7, 10, 3, 0);
    
    int passed = (match_cron(t5, &pattern) == 1 &&
                  match_cron(t10, &pattern) == 1 &&
                  match_cron(t3, &pattern) == 0);
    print_test_result("场景测试: 健康检查", passed);
}

/* ============ 主测试函数 ============ */

int main() {
    printf("\n========================================\n");
    printf("  Cron V2 单元测试\n");
    printf("========================================\n\n");
    
    printf("--- parse_range 测试 ---\n");
    test_parse_range_single_value();
    test_parse_range_multiple_values();
    test_parse_range_wildcard();
    test_parse_range_step();
    test_parse_range_range();
    test_parse_range_range_with_step();
    test_parse_range_complex();
    
    printf("\n--- parse_cron 测试 ---\n");
    test_parse_cron_5_fields();
    test_parse_cron_6_fields();
    test_parse_cron_specific_time();
    test_parse_cron_weekday();
    test_parse_cron_sunday();
    test_parse_cron_monthly();
    test_parse_cron_every_minute();
    test_parse_cron_invalid();
    
    printf("\n--- match_cron 测试 ---\n");
    test_match_cron_exact_time();
    test_match_cron_not_match();
    test_match_cron_every_hour();
    test_match_cron_weekday();
    test_match_cron_with_seconds();
    test_match_cron_step_values();
    test_match_cron_specific_month();
    test_match_cron_last_day_of_month();
    
    printf("\n--- 边界条件测试 ---\n");
    test_boundary_seconds();
    test_boundary_hours();
    test_boundary_months();
    
    printf("\n--- 实际场景测试 ---\n");
    test_scenario_backup_daily();
    test_scenario_report_weekly();
    test_scenario_cleanup_monthly();
    test_scenario_health_check();
    
    printf("\n========================================\n");
    printf("  测试结果汇总\n");
    printf("========================================\n");
    printf("通过: %d\n", test_passed);
    printf("失败: %d\n", test_failed);
    printf("总计: %d\n", test_passed + test_failed);
    printf("成功率: %.1f%%\n", 100.0 * test_passed / (test_passed + test_failed));
    printf("========================================\n\n");
    
    return test_failed > 0 ? 1 : 0;
}
