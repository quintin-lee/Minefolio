#pragma once

/**
 * @file config.h
 * @brief 轻量级 JSON 配置文件读写工具接口
 *
 * 提供不依赖庞大 JSON 解析器的简单键值对提取函数（config_get_str），
 * 以及基于 csilk JSON 构造与持久化配置文件的写入函数（config_set）。
 */

#include <stddef.h>
#include "config/secret.h"

/**
 * @brief 从指定的 JSON 配置文件中读取字符串配置项的值
 *
 * 打开并读取配置文件，以简单的双引号键名定位模式匹配目标 key，并将对应的字符串值提取至缓冲区中。
 *
 * @param[in] path JSON 配置文件的文件系统绝对或相对路径（如 "config/db.json"）
 * @param[in] key 要查找的配置项键名（如 "driver", "dsn"）
 * @param[out] out 接收配置值的字符输出缓冲区
 * @param[in] out_size 输出缓冲区容量大小（字节数）
 *
 * @return int 状态码
 * @retval 0 成功读取并写入 out 缓冲区（以 '\0' 结尾）
 * @retval -1 读取失败（文件不存在、缺少目标 key、缓冲区溢出或解析错误），此时 out 设为空字符串 ""
 *
 * @note 内存所有权：由调用方负责分配和管理 out 缓冲区。
 * @note 线程安全性：只读文件操作，但不包含跨线程/进程文件锁，多处并发修改文件时需注意同步。
 */
int config_get_str(const char* path, const char* key, char* out, size_t out_size);

/**
 * @brief 向 JSON 配置文件中写入或覆盖键值对配置
 *
 * 接收由 NULL 结尾的平铺键值对数组，自动创建父级目录并将结构体序列化为格式化 JSON 文件保存。
 *
 * @param[in] path 目标输出文件路径（如 "config/db.json"）
 * @param[in] kv 以 NULL 结尾的平铺字符串数组，格式为：key1, value1, key2, value2, ..., NULL
 *
 * @return int 状态码
 * @retval 0 写入成功
 * @retval -1 写入失败（参数无效、目录创建失败、文件无写入权限或序列化异常）
 *
 * @note 内存所有权：内部使用 csilk JSON 序列化并负责释放内存，不转移 kv 指针所有权。
 * @note 线程安全性：非原子性写入，不支持并发无锁写。
 */
int config_set(const char* path, const char** kv);
