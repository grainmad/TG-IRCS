#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

// 东八区时差（秒）
#define TIMEZONE_OFFSET (8 * 3600)

// cron字段结构
typedef struct {
    int sec[60];     // 秒 0-59
    int min[60];     // 分钟 0-59
    int hour[24];    // 小时 0-23
    int day[32];     // 日 1-31
    int month[13];   // 月 1-12
    int wday[8];     // 星期 0-7 (0和7都表示周日)
} CronPattern;

// 解析数字范围，如 "1-5" 或 "*/2"
int parse_range(const char *field, int *array, int min_val, int max_val) {
    char *token, *saveptr;
    char field_copy[256];
    strcpy(field_copy, field);
    
    // 清空数组
    for (int i = min_val; i <= max_val; i++) {
        array[i] = 0;
    }
    
    token = strtok_r(field_copy, ",", &saveptr);
    while (token != NULL) {
        // 处理 "*" 或 "*/n"
        if (token[0] == '*') {
            int step = 1;
            if (strlen(token) > 1 && token[1] == '/') {
                step = atoi(token + 2);
            }
            for (int i = min_val; i <= max_val; i += step) {
                array[i] = 1;
            }
        }
        // 处理范围 "a-b" 或 "a-b/c"
        else if (strchr(token, '-') != NULL) {
            char *dash = strchr(token, '-');
            *dash = '\0';
            int start = atoi(token);
            
            char *end_part = dash + 1;
            int end, step = 1;
            
            if (strchr(end_part, '/') != NULL) {
                char *slash = strchr(end_part, '/');
                *slash = '\0';
                end = atoi(end_part);
                step = atoi(slash + 1);
            } else {
                end = atoi(end_part);
            }
            
            for (int i = start; i <= end; i += step) {
                if (i >= min_val && i <= max_val) {
                    array[i] = 1;
                }
            }
        }
        // 处理单个数字
        else {
            int num = atoi(token);
            if (num >= min_val && num <= max_val) {
                array[num] = 1;
            }
        }
        
        token = strtok_r(NULL, ",", &saveptr);
    }
    
    return 0;
}

// 解析cron表达式
int parse_cron(const char *cron_expr, CronPattern *pattern) {
    char expr_copy[512];
    strcpy(expr_copy, cron_expr);
    
    char *fields[6];
    int field_count = 0;
    
    // 分割字段
    char *token = strtok(expr_copy, " \t");
    while (token != NULL && field_count < 6) {
        fields[field_count++] = token;
        token = strtok(NULL, " \t");
    }
    
    // 支持5字段和6字段两种格式
    if (field_count == 5) {
        // 5字段格式 (分 时 日 月 星期)
        printf("使用5字段格式: 分 时 日 月 星期\n");
        // 秒字段设为所有值
        for (int i = 0; i < 60; i++) {
            pattern->sec[i] = 1;
        }
        parse_range(fields[0], pattern->min, 0, 59);     // 分钟
        parse_range(fields[1], pattern->hour, 0, 23);    // 小时
        parse_range(fields[2], pattern->day, 1, 31);     // 日
        parse_range(fields[3], pattern->month, 1, 12);   // 月
        parse_range(fields[4], pattern->wday, 0, 7);     // 星期
    } else if (field_count == 6) {
        // 6字段格式 (秒 分 时 日 月 星期)
        printf("使用6字段格式: 秒 分 时 日 月 星期\n");
        parse_range(fields[0], pattern->sec, 0, 59);     // 秒
        parse_range(fields[1], pattern->min, 0, 59);     // 分钟
        parse_range(fields[2], pattern->hour, 0, 23);    // 小时
        parse_range(fields[3], pattern->day, 1, 31);     // 日
        parse_range(fields[4], pattern->month, 1, 12);   // 月
        parse_range(fields[5], pattern->wday, 0, 7);     // 星期
    } else {
        printf("错误: cron表达式必须有5个或6个字段\n");
        printf("5字段格式: 分 时 日 月 星期\n");
        printf("6字段格式: 秒 分 时 日 月 星期\n");
        return -1;
    }
    
    // 处理星期0和7都表示周日
    if (pattern->wday[0] || pattern->wday[7]) {
        pattern->wday[0] = pattern->wday[7] = 1;
    }
    
    return 0;
}

// 检查时间是否匹配cron模式
int match_cron(time_t timestamp, const CronPattern *pattern) {
    // 转换为东八区时间
    timestamp += TIMEZONE_OFFSET;
    struct tm *tm_info = gmtime(&timestamp);
    
    // 检查各字段是否匹配
    if (!pattern->sec[timestamp % 60]) return 0;        // 秒
    if (!pattern->min[tm_info->tm_min]) return 0;
    if (!pattern->hour[tm_info->tm_hour]) return 0;
    if (!pattern->day[tm_info->tm_mday]) return 0;
    if (!pattern->month[tm_info->tm_mon + 1]) return 0;  // tm_mon是0-11
    if (!pattern->wday[tm_info->tm_wday]) return 0;      // tm_wday是0-6
    
    return 1;
}

// 格式化时间显示（东八区）
void format_time(time_t timestamp, char *buffer) {
    timestamp += TIMEZONE_OFFSET;
    struct tm *tm_info = gmtime(&timestamp);
    strftime(buffer, 64, "%Y-%m-%d %H:%M:%S", tm_info);
}

// 显示cron模式信息
void print_cron_info(const CronPattern *pattern) {
    printf("Cron模式解析结果:\n");
    
    printf("秒: ");
    int sec_count = 0;
    for (int i = 0; i < 60; i++) {
        if (pattern->sec[i]) {
            if (sec_count < 10) printf("%d ", i);
            sec_count++;
        }
    }
    if (sec_count > 10) printf("... (共%d个值)", sec_count);
    printf("\n");
    
    printf("分钟: ");
    int min_count = 0;
    for (int i = 0; i < 60; i++) {
        if (pattern->min[i]) {
            if (min_count < 10) printf("%d ", i);
            min_count++;
        }
    }
    if (min_count > 10) printf("... (共%d个值)", min_count);
    printf("\n");
    
    printf("小时: ");
    for (int i = 0; i < 24; i++) {
        if (pattern->hour[i]) printf("%d ", i);
    }
    printf("\n");
    
    printf("日期: ");
    for (int i = 1; i <= 31; i++) {
        if (pattern->day[i]) printf("%d ", i);
    }
    printf("\n");
    
    printf("月份: ");
    for (int i = 1; i <= 12; i++) {
        if (pattern->month[i]) printf("%d ", i);
    }
    printf("\n");
    
    printf("星期: ");
    for (int i = 0; i <= 7; i++) {
        if (pattern->wday[i]) printf("%d ", i);
    }
    printf("\n");
}

int test() {
    char cron_expr[512];
    time_t timestamp;
    char time_str[64];
    
    printf("Unix时间戳Cron解析器（东八区）\n");
    printf("=====================================\n");
    
    // 输入cron表达式
    printf("请输入cron表达式:\n");
    printf("6字段格式: 秒 分 时 日 月 星期\n");
    printf("表达式: ");
    if (!fgets(cron_expr, sizeof(cron_expr), stdin)) {
        printf("读取cron表达式失败\n");
        return 1;
    }
    
    // 去掉换行符
    cron_expr[strcspn(cron_expr, "\n")] = '\0';
    
    // 解析cron表达式
    CronPattern pattern;
    if (parse_cron(cron_expr, &pattern) != 0) {
        return 1;
    }
    
    print_cron_info(&pattern);
    printf("\n");
    
    // 输入时间戳进行检查
    printf("请输入Unix时间戳 (输入0退出): ");
    while (scanf("%ld", &timestamp) == 1 && timestamp != 0) {
        format_time(timestamp, time_str);
        printf("时间: %s (东八区)\n", time_str);
        
        if (match_cron(timestamp, &pattern)) {
            printf("✓ 匹配cron表达式\n");
        } else {
            printf("✗ 不匹配cron表达式\n");
        }
        
        printf("\n请输入Unix时间戳 (输入0退出): ");
    }
    
    printf("程序结束\n");
    return 0;
}


// 测试示例
void test_examples() {
    printf("\n=== 全面测试示例 ===\n");
    
    CronPattern pattern;
    time_t test_time;
    char time_str[64];
    
    // === 基础格式测试 ===
    printf("\n【基础格式测试】\n");
    
    // 测试1: 6字段格式，每30秒执行 "*/30 * * * * *"
    printf("测试1: 每30秒执行 \"*/30 * * * * *\"\n");
    parse_cron("*/30 * * * * *", &pattern);
    test_time = 1640995200; // 2022-01-01 08:00:00 东八区
    format_time(test_time, time_str);
    printf("时间: %s -> %s\n", time_str, match_cron(test_time, &pattern) ? "匹配" : "不匹配");
    
    test_time = 1640995215; // 2022-01-01 08:00:15 东八区
    format_time(test_time, time_str);
    printf("时间: %s -> %s\n", time_str, match_cron(test_time, &pattern) ? "匹配" : "不匹配");
    
    test_time = 1640995230; // 2022-01-01 08:00:30 东八区
    format_time(test_time, time_str);
    printf("时间: %s -> %s\n", time_str, match_cron(test_time, &pattern) ? "匹配" : "不匹配");
    
    // 测试2: 5字段格式，每天9点30分 "30 9 * * *"
    printf("\n测试2: 每天9点30分 \"30 9 * * *\"\n");
    parse_cron("30 9 * * *", &pattern);
    test_time = 1640995800; // 2022-01-01 08:10:00 东八区
    format_time(test_time, time_str);
    printf("时间: %s -> %s\n", time_str, match_cron(test_time, &pattern) ? "匹配" : "不匹配");
    
    test_time = 1641000600; // 2022-01-01 09:30:00 东八区
    format_time(test_time, time_str);
    printf("时间: %s -> %s\n", time_str, match_cron(test_time, &pattern) ? "匹配" : "不匹配");
    
    // === 特殊字符测试 ===
    printf("\n【特殊字符测试】\n");
    
    // 测试3: 星号 "*" - 每分钟执行
    printf("测试3: 每分钟执行 \"* * * * *\"\n");
    parse_cron("* * * * *", &pattern);
    test_time = 1640995200; // 任意时间
    format_time(test_time, time_str);
    printf("时间: %s -> %s\n", time_str, match_cron(test_time, &pattern) ? "匹配" : "不匹配");
    
    // 测试4: 范围 "-" - 工作日9-17点每小时执行
    printf("\n测试4: 工作日9-17点每小时执行 \"0 9-17 * * 1-5\"\n");
    parse_cron("0 9-17 * * 1-5", &pattern);
    test_time = 1641196800; // 2022-01-03 14:00:00 东八区 (周一)
    format_time(test_time, time_str);
    printf("时间: %s -> %s\n", time_str, match_cron(test_time, &pattern) ? "匹配" : "不匹配");
    
    test_time = 1641225600; // 2022-01-03 22:00:00 东八区 (周一)
    format_time(test_time, time_str);
    printf("时间: %s -> %s\n", time_str, match_cron(test_time, &pattern) ? "匹配" : "不匹配");
    
    test_time = 1641369600; // 2022-01-05 14:00:00 东八区 (周三)
    format_time(test_time, time_str);
    printf("时间: %s -> %s\n", time_str, match_cron(test_time, &pattern) ? "匹配" : "不匹配");
    
    test_time = 1641542400; // 2022-01-07 14:00:00 东八区 (周五)
    format_time(test_time, time_str);
    printf("时间: %s -> %s\n", time_str, match_cron(test_time, &pattern) ? "匹配" : "不匹配");
    
    test_time = 1641628800; // 2022-01-08 14:00:00 东八区 (周六)
    format_time(test_time, time_str);
    printf("时间: %s -> %s\n", time_str, match_cron(test_time, &pattern) ? "匹配" : "不匹配");
    
    // 测试5: 列表 "," - 每天9点、12点、15点执行
    printf("\n测试5: 每天9点、12点、15点执行 \"0 9,12,15 * * *\"\n");
    parse_cron("0 9,12,15 * * *", &pattern);
    test_time = 1640998800; // 2022-01-01 09:00:00 东八区
    format_time(test_time, time_str);
    printf("时间: %s -> %s\n", time_str, match_cron(test_time, &pattern) ? "匹配" : "不匹配");
    
    test_time = 1641009600; // 2022-01-01 12:00:00 东八区
    format_time(test_time, time_str);
    printf("时间: %s -> %s\n", time_str, match_cron(test_time, &pattern) ? "匹配" : "不匹配");
    
    test_time = 1641020400; // 2022-01-01 15:00:00 东八区
    format_time(test_time, time_str);
    printf("时间: %s -> %s\n", time_str, match_cron(test_time, &pattern) ? "匹配" : "不匹配");
    
    test_time = 1641013200; // 2022-01-01 13:00:00 东八区
    format_time(test_time, time_str);
    printf("时间: %s -> %s\n", time_str, match_cron(test_time, &pattern) ? "匹配" : "不匹配");
    
    // 测试6: 步长 "/" - 每5分钟执行
    printf("\n测试6: 每5分钟执行 \"*/5 * * * *\"\n");
    parse_cron("*/5 * * * *", &pattern);
    test_time = 1640995200; // 2022-01-01 08:00:00 东八区
    format_time(test_time, time_str);
    printf("时间: %s -> %s\n", time_str, match_cron(test_time, &pattern) ? "匹配" : "不匹配");
    
    test_time = 1640995500; // 2022-01-01 08:05:00 东八区
    format_time(test_time, time_str);
    printf("时间: %s -> %s\n", time_str, match_cron(test_time, &pattern) ? "匹配" : "不匹配");
    
    test_time = 1640995320; // 2022-01-01 08:02:00 东八区
    format_time(test_time, time_str);
    printf("时间: %s -> %s\n", time_str, match_cron(test_time, &pattern) ? "匹配" : "不匹配");
    
    // === 复杂组合测试 ===
    printf("\n【复杂组合测试】\n");
    
    // 测试7: 范围+步长 - 9-17点每2小时执行
    printf("测试7: 9-17点每2小时执行 \"0 9-17/2 * * *\"\n");
    parse_cron("0 9-17/2 * * *", &pattern);
    test_time = 1640998800; // 2022-01-01 09:00:00 东八区
    format_time(test_time, time_str);
    printf("时间: %s -> %s\n", time_str, match_cron(test_time, &pattern) ? "匹配" : "不匹配");
    
    test_time = 1641006000; // 2022-01-01 11:00:00 东八区
    format_time(test_time, time_str);
    printf("时间: %s -> %s\n", time_str, match_cron(test_time, &pattern) ? "匹配" : "不匹配");
    
    test_time = 1641013200; // 2022-01-01 13:00:00 东八区
    format_time(test_time, time_str);
    printf("时间: %s -> %s\n", time_str, match_cron(test_time, &pattern) ? "匹配" : "不匹配");
    
    test_time = 1641009600; // 2022-01-01 12:00:00 东八区
    format_time(test_time, time_str);
    printf("时间: %s -> %s\n", time_str, match_cron(test_time, &pattern) ? "匹配" : "不匹配");
    
    // 测试8: 6字段复杂组合 - 工作日每10秒执行
    printf("\n测试8: 工作日每10秒执行 \"*/10 * * * * 1-5\"\n");
    parse_cron("*/10 * * * * 1-5", &pattern);
    test_time = 1641196800; // 2022-01-03 14:00:00 东八区 (周一)
    format_time(test_time, time_str);
    printf("时间: %s -> %s\n", time_str, match_cron(test_time, &pattern) ? "匹配" : "不匹配");
    
    test_time = 1641196810; // 2022-01-03 14:00:10 东八区 (周一)
    format_time(test_time, time_str);
    printf("时间: %s -> %s\n", time_str, match_cron(test_time, &pattern) ? "匹配" : "不匹配");
    
    test_time = 1641196805; // 2022-01-03 14:00:05 东八区 (周一)
    format_time(test_time, time_str);
    printf("时间: %s -> %s\n", time_str, match_cron(test_time, &pattern) ? "匹配" : "不匹配");
    
    test_time = 1641628800; // 2022-01-08 14:00:00 东八区 (周六)
    format_time(test_time, time_str);
    printf("时间: %s -> %s\n", time_str, match_cron(test_time, &pattern) ? "匹配" : "不匹配");
    
    // === 边界情况测试 ===
    printf("\n【边界情况测试】\n");
    
    // 测试9: 月份边界 - 每月1号和15号执行
    printf("测试9: 每月1号和15号执行 \"0 0 1,15 * *\"\n");
    parse_cron("0 0 1,15 * *", &pattern);
    test_time = 1640966400; // 2022-01-01 00:00:00 东八区
    format_time(test_time, time_str);
    printf("时间: %s -> %s\n", time_str, match_cron(test_time, &pattern) ? "匹配" : "不匹配");
    
    test_time = 1642176000; // 2022-01-15 00:00:00 东八区
    format_time(test_time, time_str);
    printf("时间: %s -> %s\n", time_str, match_cron(test_time, &pattern) ? "匹配" : "不匹配");
    
    test_time = 1641052800; // 2022-01-02 00:00:00 东八区
    format_time(test_time, time_str);
    printf("时间: %s -> %s\n", time_str, match_cron(test_time, &pattern) ? "匹配" : "不匹配");
    
    // 测试10: 星期边界 - 周末执行
    printf("\n测试10: 周末执行 \"0 0 * * 0,6\"\n");
    parse_cron("0 0 * * 0,6", &pattern);
    test_time = 1641139200; // 2022-01-02 00:00:00 东八区 (周日)
    format_time(test_time, time_str);
    printf("时间: %s -> %s\n", time_str, match_cron(test_time, &pattern) ? "匹配" : "不匹配");
    
    test_time = 1641571200; // 2022-01-08 00:00:00 东八区 (周六)
    format_time(test_time, time_str);
    printf("时间: %s -> %s\n", time_str, match_cron(test_time, &pattern) ? "匹配" : "不匹配");
    
    test_time = 1641225600; // 2022-01-03 00:00:00 东八区 (周一)
    format_time(test_time, time_str);
    printf("时间: %s -> %s\n", time_str, match_cron(test_time, &pattern) ? "匹配" : "不匹配");
    
    // 测试11: 年度任务 - 每年1月1日执行
    printf("\n测试11: 每年1月1日执行 \"0 0 1 1 *\"\n");
    parse_cron("0 0 1 1 *", &pattern);
    test_time = 1640966400; // 2022-01-01 00:00:00 东八区
    format_time(test_time, time_str);
    printf("时间: %s -> %s\n", time_str, match_cron(test_time, &pattern) ? "匹配" : "不匹配");
    
    test_time = 1641052800; // 2022-01-02 00:00:00 东八区
    format_time(test_time, time_str);
    printf("时间: %s -> %s\n", time_str, match_cron(test_time, &pattern) ? "匹配" : "不匹配");
    
    // === 高频任务测试 ===
    printf("\n【高频任务测试】\n");
    
    // 测试12: 每秒执行
    printf("测试12: 每秒执行 \"* * * * * *\"\n");
    parse_cron("* * * * * *", &pattern);
    test_time = 1640995200; // 任意时间
    format_time(test_time, time_str);
    printf("时间: %s -> %s\n", time_str, match_cron(test_time, &pattern) ? "匹配" : "不匹配");
    
    // 测试13: 特定秒数 - 每分钟第0、15、30、45秒执行
    printf("\n测试13: 每分钟第0、15、30、45秒执行 \"0,15,30,45 * * * * *\"\n");
    parse_cron("0,15,30,45 * * * * *", &pattern);
    test_time = 1640995200; // 2022-01-01 08:00:00 东八区
    format_time(test_time, time_str);
    printf("时间: %s -> %s\n", time_str, match_cron(test_time, &pattern) ? "匹配" : "不匹配");
    
    test_time = 1640995215; // 2022-01-01 08:00:15 东八区
    format_time(test_time, time_str);
    printf("时间: %s -> %s\n", time_str, match_cron(test_time, &pattern) ? "匹配" : "不匹配");
    
    test_time = 1640995210; // 2022-01-01 08:00:10 东八区
    format_time(test_time, time_str);
    printf("时间: %s -> %s\n", time_str, match_cron(test_time, &pattern) ? "匹配" : "不匹配");
    
    // === 实际应用场景测试 ===
    printf("\n【实际应用场景测试】\n");
    
    // 测试14: 数据库备份 - 每天凌晨2点
    printf("测试14: 数据库备份 - 每天凌晨2点 \"0 2 * * *\"\n");
    parse_cron("0 2 * * *", &pattern);
    test_time = 1640973600; // 2022-01-01 02:00:00 东八区
    format_time(test_time, time_str);
    printf("时间: %s -> %s\n", time_str, match_cron(test_time, &pattern) ? "匹配" : "不匹配");
    
    // 测试15: 日志清理 - 每周日凌晨3点
    printf("\n测试15: 日志清理 - 每周日凌晨3点 \"0 3 * * 0\"\n");
    parse_cron("0 3 * * 0", &pattern);
    test_time = 1641148800; // 2022-01-02 03:00:00 东八区 (周日)
    format_time(test_time, time_str);
    printf("时间: %s -> %s\n", time_str, match_cron(test_time, &pattern) ? "匹配" : "不匹配");
    
    test_time = 1641235200; // 2022-01-03 03:00:00 东八区 (周一)
    format_time(test_time, time_str);
    printf("时间: %s -> %s\n", time_str, match_cron(test_time, &pattern) ? "匹配" : "不匹配");
    
    // 测试16: 健康检查 - 每30秒检查一次
    printf("\n测试16: 健康检查 - 每30秒检查一次 \"*/30 * * * * *\"\n");
    parse_cron("*/30 * * * * *", &pattern);
    test_time = 1640995200; // 2022-01-01 08:00:00 东八区
    format_time(test_time, time_str);
    printf("时间: %s -> %s\n", time_str, match_cron(test_time, &pattern) ? "匹配" : "不匹配");
    
    test_time = 1640995230; // 2022-01-01 08:00:30 东八区
    format_time(test_time, time_str);
    printf("时间: %s -> %s\n", time_str, match_cron(test_time, &pattern) ? "匹配" : "不匹配");
    
    test_time = 1640995220; // 2022-01-01 08:00:20 东八区
    format_time(test_time, time_str);
    printf("时间: %s -> %s\n", time_str, match_cron(test_time, &pattern) ? "匹配" : "不匹配");
    
    printf("\n=== 测试完成 ===\n");
}

int main(int argc, char const *argv[])
{
    test_examples();
    return 0;
}
