# Minefolio Financial Core 金融数值系统架构设计与重构报告

## 1. 架构总览 (Architecture Overview)

为了彻底解决 IEEE 754 二进制浮点数（`double` / `float`）在金融计价、加权平均成本核算、跨币种外汇折算与多轮持仓买卖中引起的累积舍入误差（如 `0.1 + 0.2 != 0.3`、`19.99 * 100 = 1998.99999...`），Minefolio 建立了全新、独立、高复用、类型安全的 **Financial Core** 金融数值系统。

### 目录结构

```text
backend/src/core/financial/
├── currency.h / currency.c     # ISO 4217 货币定义、精度规范与合法性校验
├── decimal.h / decimal.c       # 128 位定点数算术核心引擎（加减乘除、银行家舍入等6种舍入模式）
├── money.h / money.c           # 货币绑定金额领域强类型（跨币种防混淆与安全比对）
├── quantity.h / quantity.c     # 标的资产份额与数量模型（支持高达 8~12 位微小数）
├── price.h / price.c           # 单位净值/价格模型与量价转换量纲算子
├── rate.h / rate.c             # 汇率/费率模型、求逆与三角套汇链式合成
└── percentage.h / percentage.c # 百分比率模型、收益率与权重计算
```

---

## 2. 强类型金融领域模型设计

```
           +-------------------------------------------------------+
           |                 128-bit Decimal Core                  |
           |        (mantissa: __int128_t, scale: int32_t)         |
           +-------------------------------------------------------+
                                      ▲
                                      |
         +----------------------------+----------------------------+
         |                            |                            |
  +--------------+             +--------------+             +--------------+
  |   money_t    |             |  quantity_t  |             |   price_t    |
  |  (amount,    |             |   (units)    |             | (unit_price, |
  |   currency)  |             +--------------+             |   currency)  |
  +--------------+                    ▲                     +--------------+
         ▲                            |                            ▲
         |     price * quantity       |      money / quantity      |
         +============================+============================+
         |     money / price                                       |
         +=========================================================+
```

### 2.1 核心数据结构

#### 1. `decimal_t` (高精度定点数)
```c
typedef struct {
    __int128_t mantissa; // 128 位带符号整数尾数（支持高达 38 位有效数字）
    int32_t    scale;    // 小数位数 (0 <= scale <= 18)
} decimal_t;
```
* **算术操作**：`decimal_add`, `decimal_sub`, `decimal_mul`, `decimal_div`
* **舍入模式**：`ROUND_HALF_UP`（四舍五入）、`ROUND_HALF_EVEN`（银行家舍入）、`ROUND_DOWN`（向零截断）、`ROUND_UP`、`ROUND_CEIL`、`ROUND_FLOOR`
* **错误防护**：`DECIMAL_ERR_DIV_BY_ZERO`, `DECIMAL_ERR_OVERFLOW`, `DECIMAL_ERR_INVALID_ARG`, `DECIMAL_ERR_PARSE`

#### 2. `money_t` (货币金额)
```c
typedef struct {
    decimal_t  amount;
    currency_t currency;
} money_t;
```
* 跨币种直接相加减自动拦截并返回 `DECIMAL_ERR_INVALID_ARG`，杜绝币种混淆漏洞。

#### 3. 量纲转换算子 (Dimensional Operators)
* $\text{Price} \times \text{Quantity} = \text{Money}$ (`price_times_quantity`)
* $\text{Money} \div \text{Quantity} = \text{Price}$ (`money_div_quantity`)
* $\text{Money} \div \text{Price} = \text{Quantity}$ (`money_div_price`)
* $\text{Money} \times \text{Rate} = \text{Converted Money}$ (`rate_convert_money`)
* $\text{Money} \times \text{Percentage} = \text{Money}$ (`percentage_apply`)
* $(\text{Part} \div \text{Whole}) \times 100 = \text{Percentage}$ (`percentage_calc`)

---

## 3. 模块迁移与覆盖情况

1. **底层数据访问层 (`common/db.h`)**：
   * 新增 `db_get_decimal`, `db_get_money`, `db_get_quantity`, `db_get_price`, `db_get_rate`, `db_get_percentage` 工具方法，优先按高精度字符串解析，避免浮点截断。
2. **核心资金与持仓层 (`common/balance.h` / `common/balance.c`)**：
   * `balance_apply_delta_m`：负债方向符号翻转及原子加减全量基于 `money_t` 执行。
   * `apply_position_fc` & `rollback_position_fc`：买入加权成本均摊、卖出成本核减、单位净值更新全量基于 `quantity_t`, `money_t`, `price_t` 定点数执行。
3. **交易类型推导层 (`common/tx_types.h` / `common/tx_types.c`)**：
   * `tx_delta_m` & `tx_effective_ldelta_m`：完全由 Financial Core 驱动。
4. **模型层 (`models/`)**：
   * `daily_expense_t` (`money_t amount`, `currency_t currency`)
   * `asset_t` (`money_t current_value`, `money_t cost_basis`, `quantity_t quantity`, `price_t net_value`)
   * `transaction_t` (`money_t amount`, `price_t price_per_unit`, `quantity_t quantity`, `currency_t currency`)
   * `transfer_t` (`money_t amount`, `currency_t currency`)
5. **外汇与服务层 (`services/market/exchange_rate_service.c`, `services/report_asset_service.c`, `services/dca_service.c`)**：
   * 多币种分桶汇总与外汇双因子归因损益（标的价格盈亏 vs 纯汇率损益）采用 `rate_t`, `money_t`, `percentage_t` 无误差核算。

---

## 4. 测试与验证报告

### 单元测试集 (Unit Tests)
已全面集成至 CMake 与 CTest（`ctest --test-dir backend/build`）：

| 测试套件 | 覆盖功能 | 状态 |
| :--- | :--- | :---: |
| `test_currency` | 币种代码大写归一化、标准小数精度、相等性及未知校验 | ✅ 100% PASS |
| `test_decimal` | 128 位加减乘除、零除拦截、0.1+0.2==0.3 精确比对、6大舍入模式、极大/微小极值 | ✅ 100% PASS |
| `test_money` | 同币种加减、跨币种加减拦截、币种规格化舍入、正负比对 | ✅ 100% PASS |
| `test_quantity` | 份额加减、加密货币 8 位微份额运算 | ✅ 100% PASS |
| `test_price` | 量价乘除量纲转换、单价推导 | ✅ 100% PASS |
| `test_rate` | 外汇金额折算、反向汇率求逆、三角套汇链式合成、百分比税费与收益率提取 | ✅ 100% PASS |
| `test_pnl` | 连续多次加仓、加权均价核算、部分减仓、分红摊薄、浮动盈亏精确验证 | ✅ 100% PASS |
| `test_fx` | 境外外币资产投资回报之「标的价格涨跌」与「纯汇率波动损益」数学恒等式验证 | ✅ 100% PASS |

### 集成测试套件 (Integration Tests)
* `test_link.sh` (33 组用例，含 133 个细分子断言)：100% PASS
* `test_ledgers.sh` (16 组用例)：100% PASS
* `test_2fa.sh` (12 组用例)：100% PASS
* `test_dca_cashflow.sh` (18 组用例)：100% PASS
* `test_ai_trace.sh` (17 组用例)：100% PASS
* `test_market_sync.sh` (18 组用例)：100% PASS
* `test_fx_oauth.sh` (20 组用例)：100% PASS
* **前端全量编译与类型检查** (`vue-tsc -b && vite build`)：零错误通过

---

## 5. API 协议与向后兼容性 (API Compatibility)

* **HTTP JSON 协议层**：维持既有 RESTful API 与响应封套，金额字段在对外输出时通过定点格式化保证精确无浮点噪声（如 `19.99` 而非 `19.989999999`）；
* **内部兼容包装器**：保留了 `balance_apply_delta`、`apply_position`、`rollback_position`、`tx_delta`、`exchange_rate_convert` 等 `double` 入参兼容签名，其底层直接转调 Financial Core 定点数核心引擎，兼顾了渐进式迁移的平滑性与计算过程的绝对精确性。
