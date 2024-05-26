
### 概述
这个文件定义了用于改进存储系统性能的`blk-switch`机制可能会用到的各种参数、结构和函数。它通过设置任务的IO优先级，判断请求是否应被处理，以及统计和管理与NVMe TCP通信有关的各种状态和参数，以达到优化存储系统性能的目的。

### 宏定义和全局变量
- `BLK_SWITCH_NR_CPUS`: 64个CPU的定义，表示系统最多支持64个CPU。
- `BLK_SWITCH_RESET_METRICS`: 1000ms，用于重置指标的时间间隔。
- `BLK_SWITCH_APPSTR_INTERVAL`: 10ms，应用程序策略间隔。
- `BLK_SWITCH_TCP_BATCH`: 16，TCP批处理的大小。
- `BLK_SWITCH_THRESH_L`: 98304字节，表示阈值。

### 模块参数
- `blk_switch_on`: 指示`blk-switch`的运行模式（0：prio，1：+reqstr，2：+appstr）。
- `blk_switch_thresh_B`: `blk-switch`的阈值B。
- `blk_switch_nr_cpus`: `blk-switch`使用的CPU数量。
- `blk_switch_debug`: `blk-switch`调试信息的开关。

### 指标变量
- `blk_switch_T_bytes`、`blk_switch_L_bytes`: 分别记录当前CPU的T字节数和L字节数。
- `blk_switch_T_metric`、`blk_switch_L_metric`: 分别记录当前CPU的T指标和L指标。
- `blk_switch_T_appstr`、`blk_switch_L_appstr`: 分别记录当前CPU的应用程序策略T和L。
- `blk_switch_reset_metrics`: 重置指标的时间戳。
- `blk_switch_last_appstr`: 最后一次应用程序策略的时间戳。
- `blk_switch_appstr_app`: 应用程序策略的应用计数。

### 请求转发统计
- `blk_switch_stats_print`、`blk_switch_stats_gen`、`blk_switch_stats_str`、`blk_switch_stats_prc`: 分别记录请求打印、生成、转发和处理的统计信息。

### 优先级和传输协议枚举
- `blk_switch_ioprio`: 表示IO优先级（T_APP和L_APP）。
- `blk_swtich_fabric`: 表示传输协议（NONE, TCP, RDMA）。

### `nvme_tcp_queue` 结构
用于描述NVMe TCP队列的结构，包含了与NVMe TCP通信有关的各种状态和参数，包括socket、工作队列、优先级、锁、发送/接收状态、命令胶囊长度、控制器指针、标志、摘要校验、i10专用变量（例如caravans和延迟的doorbells）等。

### 关键函数
1. **`blk_switch_set_ioprio`**:
    - 设置任务的IO优先级。
    - 检查任务的IO上下文并设置优先级。
    - 如果优先级为`BLK_SWITCH_L_APP`，则设置`bio`的优先级并标记为请求优先。

2. **`blk_switch_request`**:
    - 判断一个`bio`请求是否应被处理。
    - 条件包括`data`的硬件上下文启用了`blk_switch`，且`bio`请求大小大于0。

3. **`blk_switch_is_thru_request`**:
    - 判断一个`bio`请求是否是通过请求。
    - 条件是`bio`存在且优先级不为`BLK_SWITCH_L_APP`。

